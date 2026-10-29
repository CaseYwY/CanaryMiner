#include "economic_fixture.h"
#include "product_test_util.h"
#include <functional>
using namespace economic_fixture;
namespace {
using S=EconomicStatus;
std::string text(const ForecastRatio &r){std::string out;CHECK(r.format(10,&out));return out;}
void reject(const EconomicInput &base,S expected,const std::function<void(EconomicInput &)> &change){auto in=base;change(in);S status=S::Ok;CHECK(!buildEconomicStudy(in,&status)&&status==expected);}
}
int main(){
    const auto in=input();S status=S::Invalid;const auto study=buildEconomicStudy(in,&status);CHECK(study&&status==S::Ok);if(!study)return finishTests("economic study");const auto &s=study->summary();
    CHECK(s.beginUtcMs==T+1&&s.endUtcMs==T+3600001&&s.prices.expected==480&&s.prices.fxCovered==480&&s.bestPoint==0);
    CHECK(s.gpuBoardProxy&&!s.statisticalConfidenceAvailable&&s.profileRecordKey==economicProfileKey(*in.profile));
    CHECK(text(s.candidates[0].expectedTokens)=="36.0000000000"&&text(s.candidates[0].estimatedKwh)=="0.2000000000");
    CHECK(text(s.candidates[0].revenueUsd)=="0.3600000000"&&text(s.candidates[0].costUsd)=="0.1000000000"&&text(s.candidates[0].netUsd)=="0.2600000000");
    CHECK(text(s.candidates[1].expectedTokens)=="28.8000000000"&&text(s.candidates[1].costUsd)=="0.0500000000"&&text(s.candidates[1].netUsd)=="0.2380000000");
    CHECK(text(s.candidates[0].netUsdPerSecond)=="0.0000722222"); // throughput wins here even though point 1 is more energy-efficient
    {auto x=in;prices(x,"0.005");const auto r=buildEconomicStudy(x);CHECK(r&&r->summary().bestPoint==1&&text(r->summary().candidates[1].netUsd)=="0.0940000000");}
    {auto x=in;prices(x,"0.001");const auto r=buildEconomicStudy(x);CHECK(r&&!r->summary().bestPoint&&r->summary().candidates[0].netUsd.numerator.negative());}
    {auto x=in;x.policy.revenueHaircut=d("0.5");const auto r=buildEconomicStudy(x);CHECK(r&&r->summary().bestPoint==1&&text(r->summary().candidates[0].expectedTokens)=="36.0000000000");}
    {auto x=in;reward(x,"0");const auto r=buildEconomicStudy(x);CHECK(r&&!r->summary().bestPoint&&r->summary().candidates[0].expectedTokens.numerator.zero());
        x.electricity.plan.revisions[0].baseRatePerKwh={};const auto zero=buildEconomicStudy(x);CHECK(zero&&!zero->summary().bestPoint&&zero->summary().candidates[0].netUsd.numerator.zero());
        x.electricity.plan.revisions[0].baseRatePerKwh=d("-0.5");const auto negative=buildEconomicStudy(x);CHECK(negative&&negative->summary().bestPoint==0&&text(negative->summary().candidates[0].netUsd)=="0.1000000000");}
    {auto x=in;auto &a=x.candidates[0];a.transitionMs=1000;a.transitionEnergy.numerator=d("400000");x.candidates[1].repeatMeasurement=true;
        const auto r=buildEconomicStudy(x);CHECK(r&&text(r->summary().candidates[0].netUsd)=="0.2598722222"&&text(r->summary().candidates[1].netUsd)=="0.2375583333");
        CHECK(r->summary().candidates[1].testMs==5000&&r->summary().candidates[1].runMs==3595000);}
    {auto x=in;x.policy.lookbackMinutes=2;prices(x,"0.01","3");auto &p=x.pearl->minutes[1],&f=x.fx->minutes[1];p.open=p.high=p.low=p.close=d("0.03");f.open=f.high=f.low=f.close=d("1");
        const auto r=buildEconomicStudy(x);CHECK(r&&text(r->summary().candidates[0].revenueUsd)=="1.0800000000");} // mean(P*F) != mean(P)*mean(F)
    {auto x=in;x.electricity.plan.currency="EUR";x.electricity.fx=ForecastCurrencyFx{ForecastQualification::SyntheticFixture,"EUR","fixture.fx",d("2"),T,1000};
        const auto r=buildEconomicStudy(x);CHECK(r&&r->summary().bestPoint==1&&text(r->summary().candidates[0].costUsd)=="0.2000000000");}
    {auto x=in;x.candidates[0].permitted=false;const auto r=buildEconomicStudy(x);CHECK(r&&r->summary().candidates.size()==1&&r->summary().bestPoint==1);
        x.candidates[1].permitted=false;const auto none=buildEconomicStudy(x);CHECK(none&&none->summary().candidates.empty()&&!none->summary().bestPoint);}
    {auto x=input(profile("200","200",2000,2000,100000,100000));const auto r=buildEconomicStudy(x);CHECK(r&&r->summary().bestPoint==0);}
    // A record key changes with measured evidence, even when the plan and its identity stay the same.
    {auto changed=in.profile->input();changed.samples.back().userTmacs=d("361");auto p=buildWorkProfile(changed);CHECK(p&&economicProfileKey(*p)!=economicProfileKey(*in.profile));
        auto x=in;x.profile=p;CHECK(!buildEconomicStudy(x,&status)&&status==S::Attribution);}
    reject(in,S::Disabled,[](auto &x){x.policy.enabled=false;});reject(in,S::Disabled,[](auto &x){x.currentPlan.enabled=false;});
    for(auto change:std::vector<std::function<void(EconomicInput &)>>{
        [](auto &x){x.generation=0;},[](auto &x){x.policy.horizonMs=0;},[](auto &x){x.policy.horizonMs=86400001;},[](auto &x){x.policy.lookbackMinutes=0;},
        [](auto &x){x.policy.lookbackMinutes=720;},[](auto &x){x.policy.maximumMarketAgeMs=0;},[](auto &x){x.policy.maximumMarketAgeMs=86400001;},
        [](auto &x){x.policy.maximumFeedSkewMs=86400001;},[](auto &x){x.policy.revenueHaircut=d("-0.01");},[](auto &x){x.policy.revenueHaircut=d("1.01");},
        [](auto &x){x.candidates.pop_back();},[](auto &x){x.candidates[1].declared=false;},[](auto &x){x.candidates[1].transitionEnergy.numerator=d("-1");},
        [](auto &x){x.candidates[1].transitionEnergy.numerator=d("1");},[](auto &x){x.candidates[1].key.clear();},[](auto &x){x.candidates[1].validForMs=0;},
        [](auto &x){x.electricity.fx=ForecastCurrencyFx{};}})reject(in,S::Invalid,change);
    for(auto change:std::vector<std::function<void(EconomicInput &)>>{
        [](auto &x){x.profile.reset();},[](auto &x){x.reward.reset();},[](auto &x){x.pearl.reset();},[](auto &x){x.fx.reset();},[](auto &x){x.electricity.configured=false;},
        [](auto &x){x.electricity.plan.currency="USDT";},[](auto &x){x.electricity.plan.revisions[0].publishedUtcMs++;},[](auto &x){x.electricity.plan.declared=false;}})reject(in,S::Unavailable,change);
    for(auto change:std::vector<std::function<void(EconomicInput &)>>{
        [](auto &x){x.currentPlan.measurementMs++;},[](auto &x){x.currentPlan.identity.environmentKey+=".changed";},[](auto &x){x.production.scopeId+=".other";},
        [](auto &x){x.fx->observation.generation++;},[](auto &x){x.pearl->observation.series=MarketSeries::KrakenUsdtUsdV1;},[](auto &x){x.electricity.opening.evidence.adapter+=".other";},
        [](auto &x){auto c=calibration(x);c.profileKey="fixture.old-plan";c.periods[0].work.profileKey=c.profileKey;x.reward=calibrateReward(c);}})reject(in,S::Attribution,change);
    for(auto change:std::vector<std::function<void(EconomicInput &)>>{
        [](auto &x){x.production.scope=CreditScope::Account;},[](auto &x){x.production.qualification=CreditQualification::Unsupported;},
        [](auto &x){x.pearl->observation.origin=MarketOrigin::ProviderResponse;},[](auto &x){x.fx->convention=MarketMinuteConvention::Unqualified;},
        [](auto &x){x.electricity.plan.origin=TariffOrigin::UserDeclaredOffline;},[](auto &x){x.electricity.plan.tiers=TierMode::AllUnits;},
        [](auto &x){x.electricity.plan.minimumCycleSubtotal=d("0");},[](auto &x){x.electricity.plan.cycleCharge=d("1");},
        [](auto &x){x.electricity.plan.revisions[0].timeMode=TimeRateMode::Dynamic;},[](auto &x){x.electricity.plan.subtotalMultiplier=d("1.1");},
        [](auto &x){x.electricity.plan.revisions[0].chargePerCivilDay=d("1");},[](auto &x){x.electricity.plan.unsupportedRules=1;},
        [](auto &x){x.electricity.plan.clock={CivilClockKind::NamedIana,"fixture.zone",0};}})reject(in,S::Unsupported,change);
    for(auto change:std::vector<std::function<void(EconomicInput &)>>{
        [](auto &x){x.conditions.drift=true;},[](auto &x){x.nowMonoMs+=120001;},[](auto &x){x.pearl->observation.receivedMonoMs-=30001;},
        [](auto &x){x.fx->observation.observedUtc-=31;x.fx->observation.requestStartedUtc-=31;},[](auto &x){x.candidates[1].observedUtcMs-=120001;},
        [](auto &x){x.nowUtcMs+=86400001;}})reject(in,S::Stale,change);
    for(auto change:std::vector<std::function<void(EconomicInput &)>>{
        [](auto &x){x.nowUtcMs=-1;},[](auto &x){x.nowUtcMs=253402300799999LL;},[](auto &x){x.nowUtcMs--;},
        [](auto &x){x.fx->observation.observedUtc++;},[](auto &x){x.fx->observation.requestStartedUtc++;},[](auto &x){x.fx->observation.receivedMonoMs++;},
        [](auto &x){x.candidates[1].observedUtcMs++;}})reject(in,S::Clock,change);
    for(auto change:std::vector<std::function<void(EconomicInput &)>>{
        [](auto &x){x.conditions.fault=true;},[](auto &x){x.conditions.safetyTelemetryValid=false;},[](auto &x){x.conditions.thermalSafe=false;}})reject(in,S::Unsafe,change);
    reject(in,S::Game,[](auto &x){x.conditions.gameActive=true;});reject(in,S::Game,[](auto &x){x.conditions.gameStateKnown=false;});
    for(auto change:std::vector<std::function<void(EconomicInput &)>>{
        [](auto &x){x.fx->minutes.pop_back();},[](auto &x){x.pearl->minutes[1].closed=false;},[](auto &x){x.fx->minutes[1].startUtc++;},
        [](auto &x){x.electricity.opening.throughUtcMs++;},[](auto &x){x.candidates[1].transitionMs=x.policy.horizonMs;},
        [](auto &x){x.candidates[1].transitionMs=x.policy.horizonMs-1;x.candidates[1].repeatMeasurement=true;}})reject(in,S::Coverage,change);
    // Failure in a permitted losing candidate still prevents a misleading best-of-complete-set claim.
    reject(in,S::Capacity,[](auto &x){x.pearl->minutes.resize(1001);});
    reject(in,S::Capacity,[](auto &x){reward(x,std::string(96,'9'));});
    reject(in,S::Capacity,[](auto &x){x.candidates[1].permitted=false;x.candidates[1].key=std::string(129,'x');});
    {auto x=in;reward(x,std::string(60,'9'));x.candidates[1].permitted=false;CHECK(static_cast<bool>(buildEconomicStudy(x)));
        x.candidates[1].permitted=true;CHECK(!buildEconomicStudy(x,&status)&&status==S::Capacity);} // unrepresentable exact comparison cannot pick an arbitrary winner
    {auto x=in;x.policy.lookbackMinutes=719;prices(x);const auto r=buildEconomicStudy(x);CHECK(r&&r->summary().prices.expected==719&&r->summary().bestPoint==0);}
    {auto x=in;x.electricity.plan.currency="EUR";x.electricity.fx=ForecastCurrencyFx{ForecastQualification::SyntheticFixture,"EUR","fixture.fx",d("2"),T-1001,1000};
        CHECK(!buildEconomicStudy(x,&status)&&status==S::Stale);x.electricity.fx->observedUtcMs=T;x.electricity.fx->baseCurrency="USD";CHECK(!buildEconomicStudy(x,&status)&&status==S::Invalid);}
    std::atomic<bool> stop{true};CHECK(!buildEconomicStudy(in,&status,&stop)&&status==S::Cancelled);
    {auto x=in;x.policy.revenueHaircut=d("1");const auto r=buildEconomicStudy(x);CHECK(r&&!r->summary().bestPoint&&r->summary().candidates[0].revenueUsd.numerator.zero());}
    CHECK(study->input().policy.lookbackMinutes==480&&study->input().candidates[0].transitionMs==0);
    return finishTests("economic study");
}
