#include "economic_study.h"
#include "hash.h"
#include <algorithm>
namespace om {
namespace {
using S=EconomicStatus;
constexpr uint64_t day=86400000;constexpr int64_t lastUtc=253402300799999LL;
ExactDecimal n(uint64_t value){ExactDecimal out;ExactDecimal::parse(std::to_string(value),&out);return out;}
bool instant(int64_t value){return value>=0&&value<=lastUtc;}
bool bounded(const ExactDecimal &value){return value.digits()<=38&&value.scale()<=18;}
bool key(const std::string &s){return s.size()>8&&s.size()<=128&&s.rfind("fixture.",0)==0&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});}
bool same(const PowerEvidence &a,const PowerEvidence &b){return a.basis==b.basis&&a.origin==b.origin&&a.scope==b.scope&&a.adapter==b.adapter;}
bool cancelled(const std::atomic<bool> *c){return c&&c->load();}
uint64_t difference(uint64_t a,uint64_t b){return a>b?a-b:b-a;}
bool multiply(ExactDecimal &a,const ExactDecimal &b){return a.multiply(b,&a);}
S observation(const MarketCandles &c,MarketSeries series,const EconomicInput &in){const auto &o=c.observation;
    if(o.origin!=MarketOrigin::SyntheticFixture||c.convention!=MarketMinuteConvention::FixtureMinuteStart)return S::Unsupported;
    if(o.series!=series||o.generation!=in.generation)return S::Attribution;
    if(!o.requestStartedUtc||o.requestStartedUtc>o.observedUtc||o.observedUtc>static_cast<uint64_t>(in.nowUtcMs/1000)||o.receivedMonoMs>in.nowMonoMs)return S::Clock;
    if(static_cast<uint64_t>(in.nowUtcMs)-o.observedUtc*1000>in.policy.maximumMarketAgeMs||in.nowMonoMs-o.receivedMonoMs>in.policy.maximumMarketAgeMs)return S::Stale;
    return c.minutes.size()>1000?S::Capacity:S::Ok;
}
S electricity(const EconomicInput &in,int64_t begin,int64_t end,ExactDecimal &rate){const auto &e=in.electricity;const auto &p=e.plan;
    if(!e.configured)return S::Unavailable;
    if(p.origin!=TariffOrigin::SyntheticFixture||e.opening.evidence.origin!=PowerOrigin::SyntheticFixture)return S::Unsupported;
    if(!same(e.opening.evidence,in.currentPlan.identity.power))return S::Attribution;
    // Flat board-proxy slice only. Nonlinear/whole-meter costs require full matched trajectories in a separate adapter.
    if(e.opening.evidence.basis!=PowerBasis::GpuBoard||p.revisions.size()!=1||p.tiers!=TierMode::None||!p.cycleCharge.zero()||p.minimumCycleSubtotal||
        !p.subtotalMultiplier||p.subtotalMultiplier->compare(n(1))||p.unsupportedRules||p.clock.kind==CivilClockKind::NamedIana)return S::Unsupported;
    const auto &r=p.revisions.front();
    if(r.timeMode!=TimeRateMode::Constant||!r.calendar.empty()||!r.dynamic.empty()||!r.bands.empty()||!r.chargePerCivilDay.zero())return S::Unsupported;
    if(!bounded(r.baseRatePerKwh))return S::Capacity;
    if(e.opening.throughUtcMs!=begin)return S::Coverage;
    const auto validation=evaluateBill(p,e.opening,{e.opening.evidence,{{begin,end,n(0)}}},end,in.nowUtcMs);
    if(validation.status!=ElectricityStatus::Ok||!validation.charge)return validation.status==ElectricityStatus::Capacity?S::Capacity:S::Unavailable;
    rate=r.baseRatePerKwh;
    if(p.currency=="USD")return e.fx?S::Invalid:S::Ok;
    if(!e.fx)return S::Unavailable;
    const auto &f=*e.fx;
    if(f.qualification!=ForecastQualification::SyntheticFixture||!key(f.source))return S::Unsupported;
    if(f.baseCurrency!=p.currency||f.usdPerUnit.negative()||f.usdPerUnit.zero()||!bounded(f.usdPerUnit)||!f.maxAgeMs||f.maxAgeMs>day)return S::Invalid;
    if(!instant(f.observedUtcMs)||f.observedUtcMs>in.nowUtcMs)return S::Clock;
    if(static_cast<uint64_t>(in.nowUtcMs-f.observedUtcMs)>f.maxAgeMs)return S::Stale;
    return multiply(rate,f.usdPerUnit)?S::Ok:S::Capacity;
}
S candidate(const EconomicInput &in,const EconomicSummary &summary,size_t index,const ExactDecimal &rate,EconomicCandidate &out){
    const auto &a=in.candidates[index];const auto &p=in.profile->summary().points[index];
    if(!a.declared||!key(a.key)||!a.validForMs||a.validForMs>day||a.transitionEnergy.numerator.negative()||!bounded(a.transitionEnergy.numerator)||
        (!a.transitionMs&&!a.transitionEnergy.numerator.zero()))return S::Invalid;
    if(!instant(a.observedUtcMs)||a.observedUtcMs>in.nowUtcMs)return S::Clock;
    if(static_cast<uint64_t>(in.nowUtcMs-a.observedUtcMs)>a.validForMs)return S::Stale;
    out.point=index;out.hostWaitMs=p.hostWaitMs;out.elapsedMs=in.policy.horizonMs;out.transitionMs=a.transitionMs;
    out.testMs=a.repeatMeasurement?in.profile->summary().elapsedMs:0;
    if(out.transitionMs>out.elapsedMs||out.testMs>out.elapsedMs-out.transitionMs)return S::Coverage;
    out.runMs=out.elapsedMs-out.transitionMs-out.testMs;
    if(!out.runMs)return S::Coverage;
    const auto &cal=in.reward->calibration();const auto &price=*summary.prices.tokenUsd;
    // Calibration uses TMAC/s * ms; measured counters use completed TMAC, hence the explicit factor 1000.
    auto &tokens=out.expectedTokens;tokens.numerator=cal.creditedTokens;tokens.denominator=cal.userTmacMilliseconds;
    if(!multiply(tokens.numerator,p.userTmacs)||!multiply(tokens.numerator,n(1000))||!multiply(tokens.numerator,n(out.runMs))||!multiply(tokens.denominator,n(p.measuredMs)))return S::Capacity;
    auto &revenue=out.revenueUsd;revenue=tokens;ExactDecimal keep;
    if(!n(1).subtract(in.policy.revenueHaircut,&keep)||!multiply(revenue.numerator,price.sum)||!multiply(revenue.numerator,keep)||!multiply(revenue.denominator,n(price.count)))return S::Capacity;
    ExactDecimal overhead=a.transitionEnergy.numerator;
    if(a.repeatMeasurement&&!overhead.add(in.profile->summary().totalEnergy.numerator,&overhead))return S::Capacity;
    auto &energy=out.estimatedKwh;energy.numerator=p.energy.numerator;energy.denominator=n(p.measuredMs);
    if(!multiply(energy.numerator,n(out.runMs))||!multiply(overhead,n(p.measuredMs))||!energy.numerator.add(overhead,&energy.numerator)||!multiply(energy.denominator,n(wattMsPerKwh)))return S::Capacity;
    out.costUsd=energy;if(!multiply(out.costUsd.numerator,rate))return S::Capacity;
    // Both terms share measuredMs. Cancel that known common factor before exact subtraction.
    ExactDecimal income=revenue.numerator,expense=out.costUsd.numerator;
    if(!multiply(income,n(wattMsPerKwh))||!multiply(expense,cal.userTmacMilliseconds)||!multiply(expense,n(price.count))||
        !income.subtract(expense,&out.netUsd.numerator))return S::Capacity;
    out.netUsd.denominator=revenue.denominator;if(!multiply(out.netUsd.denominator,n(wattMsPerKwh)))return S::Capacity;
    out.netUsdPerSecond=out.netUsd;
    return multiply(out.netUsdPerSecond.numerator,n(1000))&&multiply(out.netUsdPerSecond.denominator,n(out.elapsedMs))?S::Ok:S::Capacity;
}
}
std::string economicProfileKey(const WorkProfile &p){const auto &in=p.input();
    std::string text="CMECONPROFILE1|"+workProfilePlanFingerprint(in.plan);
    auto add=[&](uint64_t v){text+="|"+std::to_string(v);};add(in.generation);add(in.counterEpoch);add(in.samples.size());
    for(const auto &s:in.samples){add(s.generation);add(s.counterEpoch);add(s.monotonicMs);add(static_cast<uint64_t>(s.utcMs));add(s.appliedHostWaitMs);
        add(s.completeWorkBoundary);add(s.safetyTelemetryValid);add(s.thermalSafe);add(s.gameStateKnown);add(s.gameActive);add(s.fault);add(*s.milliwatts);
        text+="|"+s.fingerprint+"|"+s.userTmacs->text()+"|"+s.donationTmacs->text();}
    add(in.windows.size());for(const auto &w:in.windows){add(w.candidate);add(w.repetition);add(w.beginSample);add(w.measurementSample);add(w.endSample);}
    return "fixture.work."+sha256Hex(text);
}
std::shared_ptr<const EconomicStudy> buildEconomicStudy(const EconomicInput &in,S *status,const std::atomic<bool> *cancel){
    auto fail=[&](S s)->std::shared_ptr<const EconomicStudy>{if(status)*status=s;return {};};
    if(cancelled(cancel))return fail(S::Cancelled);
    if(!in.policy.enabled)return fail(S::Disabled);
    try{
        if(!in.generation||!in.policy.horizonMs||in.policy.horizonMs>day||!in.policy.lookbackMinutes||in.policy.lookbackMinutes>719||
            !in.policy.maximumMarketAgeMs||in.policy.maximumMarketAgeMs>day||in.policy.maximumFeedSkewMs>day||!bounded(in.policy.revenueHaircut)||
            in.policy.revenueHaircut.negative()||in.policy.revenueHaircut.compare(n(1))>0)return fail(S::Invalid);
        if(!instant(in.nowUtcMs)||in.nowUtcMs>=lastUtc||in.policy.horizonMs>static_cast<uint64_t>(lastUtc-in.nowUtcMs-1))return fail(S::Clock);
        if(!in.profile||!in.reward)return fail(S::Unavailable);
        if(!in.currentPlan.enabled)return fail(S::Disabled);
        if(workProfilePlanFingerprint(in.currentPlan)!=workProfilePlanFingerprint(in.profile->input().plan))return fail(S::Attribution);
        if(in.conditions.fault||!in.conditions.safetyTelemetryValid||!in.conditions.thermalSafe)return fail(S::Unsafe);
        if(!in.conditions.gameStateKnown||in.conditions.gameActive)return fail(S::Game);
        if(in.conditions.drift)return fail(S::Stale);
        if(in.profile->summary().endUtcMs>in.nowUtcMs||in.reward->input().calibratedUtcMs>in.nowUtcMs)return fail(S::Clock);
        if(static_cast<uint64_t>(in.nowUtcMs-in.profile->summary().endUtcMs)>in.currentPlan.validForMs||in.nowUtcMs>in.reward->input().validUntilUtcMs)return fail(S::Stale);
        if(in.production.qualification!=CreditQualification::SyntheticFixture||in.production.scope!=CreditScope::Worker)return fail(S::Unsupported);
        if(!sameCreditSourceContract(in.production,in.reward->input().binding))return fail(S::Attribution);
        EconomicSummary summary;summary.beginUtcMs=in.nowUtcMs+1;summary.endUtcMs=summary.beginUtcMs+static_cast<int64_t>(in.policy.horizonMs);
        summary.profileRecordKey=economicProfileKey(*in.profile);
        if(in.reward->input().profileKey!=summary.profileRecordKey)return fail(S::Attribution);
        if(in.candidates.size()!=in.profile->summary().points.size())return fail(S::Invalid);
        for(const auto &a:in.candidates)if(a.key.size()>128)return fail(S::Capacity); // excluded inputs are still retained in the immutable record
        if(!in.pearl||!in.fx)return fail(S::Unavailable);
        auto s=observation(*in.pearl,MarketSeries::GatePearlMainnetUsdtV1,in);if(s!=S::Ok)return fail(s);
        s=observation(*in.fx,MarketSeries::KrakenUsdtUsdV1,in);if(s!=S::Ok)return fail(s);
        const auto &p=in.pearl->observation,&f=in.fx->observation;
        if(difference(p.observedUtc,f.observedUtc)*1000>in.policy.maximumFeedSkewMs||difference(p.receivedMonoMs,f.receivedMonoMs)>in.policy.maximumFeedSkewMs)return fail(S::Stale);
        const auto end=static_cast<uint64_t>(in.nowUtcMs/60000)*60,span=static_cast<uint64_t>(in.policy.lookbackMinutes)*60;
        if(end<=span)return fail(S::Clock);
        summary.prices=marketWindow(*in.pearl,&*in.fx,end-span,end);
        if(summary.prices.status!=MarketDataStatus::Ok||!summary.prices.tokenUsd)return fail(summary.prices.status==MarketDataStatus::Arithmetic?S::Capacity:S::Coverage);
        ExactDecimal rate;s=electricity(in,summary.beginUtcMs,summary.endUtcMs,rate);if(s!=S::Ok)return fail(s);
        const EconomicCandidate *best=nullptr;summary.candidates.reserve(in.candidates.size());
        for(size_t i=0;i<in.candidates.size();++i){if(cancelled(cancel))return fail(S::Cancelled);if(!in.candidates[i].permitted)continue;
            EconomicCandidate c;s=candidate(in,summary,i,rate,c);if(s!=S::Ok)return fail(s);summary.candidates.push_back(std::move(c));const auto &next=summary.candidates.back();
            if(next.netUsd.numerator.negative()||next.netUsd.numerator.zero())continue;
            if(!best){best=&next;continue;}
            ExactDecimal left=next.netUsdPerSecond.numerator,right=best->netUsdPerSecond.numerator;
            if(!multiply(left,best->netUsdPerSecond.denominator)||!multiply(right,next.netUsdPerSecond.denominator))return fail(S::Capacity);
            if(left.compare(right)>0)best=&next; // exact ties retain the earlier explicitly ordered point
        }
        if(best)summary.bestPoint=best->point;
        if(cancelled(cancel))return fail(S::Cancelled);
        auto study=std::shared_ptr<const EconomicStudy>(new EconomicStudy(in,std::move(summary)));
        if(cancelled(cancel))return fail(S::Cancelled);
        if(status)*status=S::Ok;
        return study;
    }catch(...){return fail(S::Capacity);}
}
} // namespace om
