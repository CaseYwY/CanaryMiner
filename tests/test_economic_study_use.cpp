// Session-bound use of genuine immutable synthetic studies. No sampler, miner, network or financial source.
#include "economic_fixture.h"
#include "product/economic_study_use.h"
#include "product_test_util.h"
#include <functional>
using namespace economic_fixture;
namespace {
using S=EconomicUseState;
struct Fixture {
    SessionIntent intent;SessionToken token=intent.explicitStart();EconomicInput in=input();
    EconomicUsePolicy policy{true,5000};
    Fixture(){in.generation=token.epoch;prices(in);}
    OptimizationTime at(uint64_t ms=0)const{return {in.nowUtcMs+static_cast<int64_t>(ms),in.nowMonoMs+ms};}
    std::shared_ptr<const EconomicStudy> build(){EconomicStatus status;auto study=buildEconomicStudy(in,&status);
        if(!study)std::fprintf(stderr,"economic fixture rejected, status %u\n",static_cast<unsigned>(status));
        CHECK(study!=nullptr);return study;}
};
void basic(){
    Fixture f;const auto study=f.build();EconomicStudyUse use(study,f.policy,f.intent,f.token);
    CHECK(use.snapshot().state==S::Dormant&&!use.snapshot().point);
    const auto first=use.check(f.in.currentPlan,f.at(),f.in.conditions);
    CHECK(first.state==S::Eligible&&first.epoch==f.token.epoch&&first.point==0&&first.hostWaitMs==0);
    CHECK(first.evaluated.utcMs==T&&first.evaluated.monotonicMs==f.in.nowMonoMs&&first.validUntilMonoMs==f.in.nowMonoMs+5000);
    CHECK(use.check(f.in.currentPlan,f.at(4999),f.in.conditions).state==S::Eligible);
    CHECK(use.check(f.in.currentPlan,f.at(5000),f.in.conditions).state==S::Expired&&!use.snapshot().point&&!use.snapshot().hostWaitMs&&!use.snapshot().validUntilMonoMs);
    CHECK(use.check(f.in.currentPlan,f.at(5001),f.in.conditions).state==S::Expired);
    EconomicStudyUse late(study,f.policy,f.intent,f.token);CHECK(late.check(f.in.currentPlan,f.at(5000),f.in.conditions).state==S::Expired);
    EconomicStudyUse stopped(study,f.policy,f.intent,f.token);stopped.requestStop();CHECK(stopped.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Stopped);
    EconomicStudyUse off(study,{},f.intent,f.token);CHECK(off.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Disabled);
    for(uint64_t age:{0,86400001}){EconomicStudyUse invalid(study,{true,age},f.intent,f.token);CHECK(invalid.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Invalid);}
    EconomicStudyUse empty({},f.policy,f.intent,f.token);CHECK(empty.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Invalid);
    EconomicStudyUse foreign(study,f.policy,f.intent,{f.token.epoch+1});CHECK(foreign.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Stopped);
    auto wrong=f.in;wrong.generation++;prices(wrong);EconomicStudyUse wrongEpoch(buildEconomicStudy(wrong),f.policy,f.intent,f.token);
    CHECK(wrongEpoch.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Attribution);
    EconomicStudyUse normal(study,f.policy,f.intent,f.token);CHECK(normal.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Eligible);
    f.intent.stop();CHECK(normal.check(f.in.currentPlan,f.at(1),f.in.conditions).state==S::Stopped);
    f.intent.explicitStart();CHECK(normal.check(f.in.currentPlan,f.at(2),f.in.conditions).state==S::Stopped);
}
void originalBounds(){
    // Tighten each independent source alone. An unchanged decision timestamp cannot hide its earlier expiry.
    const std::vector<std::function<void(Fixture &)>> changes={
        [](auto &f){f.policy.maximumResultAgeMs=500;},
        [](auto &f){f.in.policy.maximumMarketAgeMs=500;},
        [](auto &f){f.in.nowUtcMs+=1000;f.in.electricity.opening.cycleBeginUtcMs=f.in.electricity.opening.throughUtcMs=f.in.nowUtcMs+1;prices(f.in);f.in.policy.maximumMarketAgeMs=1500;
            f.in.pearl->observation.observedUtc--;f.in.pearl->observation.requestStartedUtc--;},
        [](auto &f){f.in.nowUtcMs+=1000;f.in.electricity.opening.cycleBeginUtcMs=f.in.electricity.opening.throughUtcMs=f.in.nowUtcMs+1;prices(f.in);f.in.policy.maximumMarketAgeMs=1500;
            f.in.fx->observation.observedUtc--;f.in.fx->observation.requestStartedUtc--;},
        [](auto &f){f.in.policy.maximumMarketAgeMs=1500;f.in.pearl->observation.receivedMonoMs-=1000;},
        [](auto &f){f.in.policy.maximumMarketAgeMs=1500;f.in.fx->observation.receivedMonoMs-=1000;},
        [](auto &f){auto measured=f.in.profile->input();measured.plan.validForMs=500;f.in.profile=buildWorkProfile(measured);f.in.currentPlan=measured.plan;reward(f.in);},
        [](auto &f){auto c=calibration(f.in);c.validUntilUtcMs=T+500;f.in.reward=calibrateReward(c);},
        [](auto &f){f.in.candidates[0].validForMs=500;},
        [](auto &f){f.in.candidates[1].validForMs=500;}, // even the losing permitted candidate constrains the complete comparison
        [](auto &f){f.in.electricity.plan.currency="EUR";f.in.electricity.fx=ForecastCurrencyFx{ForecastQualification::SyntheticFixture,"EUR","fixture.fx",d("1"),T,500};},
        [](auto &f){f.in.policy.horizonMs=499;}
    };
    for(size_t index=0;index<changes.size();++index){Fixture f;changes[index](f);auto study=f.build();
        if(!study){std::fprintf(stderr,"dependency fixture %zu\n",index);continue;}EconomicStudyUse use(study,f.policy,f.intent,f.token);
        CHECK(use.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Eligible&&use.snapshot().validUntilMonoMs==f.in.nowMonoMs+500);
        CHECK(use.check(f.in.currentPlan,f.at(499),f.in.conditions).state==S::Eligible);
        CHECK(use.check(f.in.currentPlan,f.at(500),f.in.conditions).state==S::Expired&&!use.snapshot().point);
        EconomicStudyUse delayed(study,f.policy,f.intent,f.token);auto now=f.at(499);now.utcMs=f.in.nowUtcMs+100;
        CHECK(delayed.check(f.in.currentPlan,now,f.in.conditions).validUntilMonoMs==f.in.nowMonoMs+500);
        now=f.at(500);now.utcMs=f.in.nowUtcMs+101;CHECK(delayed.check(f.in.currentPlan,now,f.in.conditions).state==S::Expired);}
    {Fixture f;f.in.candidates[1].permitted=false;f.in.candidates[1].validForMs=1;auto study=f.build();EconomicStudyUse use(study,f.policy,f.intent,f.token);
        CHECK(use.check(f.in.currentPlan,f.at(2),f.in.conditions).state==S::Eligible&&use.snapshot().validUntilMonoMs==f.in.nowMonoMs+5000);}
}
void changedConditions(){
    const std::vector<std::pair<S,std::function<void(WorkProfileConditions &)>>> changes={
        {S::Unsafe,[](auto &c){c.safetyTelemetryValid=false;}},{S::Unsafe,[](auto &c){c.thermalSafe=false;}},{S::Unsafe,[](auto &c){c.fault=true;}},
        {S::Game,[](auto &c){c.gameStateKnown=false;}},{S::Game,[](auto &c){c.gameActive=true;}},{S::Drift,[](auto &c){c.drift=true;}}
    };
    for(const auto &change:changes){Fixture f;EconomicStudyUse use(f.build(),f.policy,f.intent,f.token);
        CHECK(use.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Eligible);auto conditions=f.in.conditions;change.second(conditions);
        CHECK(use.check(f.in.currentPlan,f.at(100),conditions).state==change.first&&!use.snapshot().point&&!use.snapshot().validUntilMonoMs);
        CHECK(use.check(f.in.currentPlan,f.at(200),f.in.conditions).state==change.first);
        f.intent.stop();CHECK(use.check(f.in.currentPlan,f.at(300),f.in.conditions).state==S::Stopped);}
    for(const auto &change:std::vector<std::function<void(WorkProfilePlan &)>>{
        [](auto &p){p.enabled=false;},[](auto &p){p.identity.driverVersion+=".changed";},[](auto &p){p.hostWaitMs[1]=11;},
        [](auto &p){++p.validForMs;},[](auto &p){++p.measurementMs;},[](auto &p){p.maximumEnergy.numerator=d("1");}}){
        Fixture f;EconomicStudyUse use(f.build(),f.policy,f.intent,f.token);CHECK(use.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::Eligible);
        auto p=f.in.currentPlan;change(p);CHECK(use.check(p,f.at(100),f.in.conditions).state==S::Attribution);
        CHECK(use.check(f.in.currentPlan,f.at(200),f.in.conditions).state==S::Attribution);}
}
void clocksAndNoCandidate(){
    {Fixture f;EconomicStudyUse use(f.build(),f.policy,f.intent,f.token);auto t=f.at(100);t.utcMs+=100;
        CHECK(use.check(f.in.currentPlan,t,f.in.conditions).validUntilMonoMs==f.in.nowMonoMs+4900);
        CHECK(use.check(f.in.currentPlan,f.at(300),f.in.conditions).validUntilMonoMs==f.in.nowMonoMs+4900);
        CHECK(use.check(f.in.currentPlan,f.at(4899),f.in.conditions).state==S::Eligible);
        CHECK(use.check(f.in.currentPlan,f.at(4900),f.in.conditions).state==S::Expired);}
    for(unsigned mode=0;mode<5;++mode){Fixture f;EconomicStudyUse use(f.build(),f.policy,f.intent,f.token);
        CHECK(use.check(f.in.currentPlan,f.at(100),f.in.conditions).state==S::Eligible);auto t=f.at(200);
        if(mode==0)t.utcMs=T+99;
        if(mode==1)t.monotonicMs=f.in.nowMonoMs+99;
        if(mode==2)t.utcMs+=2001;
        if(mode==3)t.monotonicMs+=2001;
        if(mode==4)t.utcMs=253402300800000LL;
        CHECK(use.check(f.in.currentPlan,t,f.in.conditions).state==S::ClockFault&&!use.snapshot().validUntilMonoMs);
        CHECK(use.check(f.in.currentPlan,f.at(300),f.in.conditions).state==S::ClockFault);}
    {Fixture f;f.in.nowMonoMs=UINT64_MAX-1000;prices(f.in);EconomicStudyUse use(f.build(),f.policy,f.intent,f.token);
        CHECK(use.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::ClockFault);}
    for(unsigned mode=0;mode<3;++mode){Fixture f;
        if(mode==0)prices(f.in,"0.001");
        if(mode==1){reward(f.in,"0");f.in.electricity.plan.revisions[0].baseRatePerKwh={};}
        if(mode==2)for(auto &c:f.in.candidates)c.permitted=false;
        EconomicStudyUse use(f.build(),f.policy,f.intent,f.token);CHECK(use.check(f.in.currentPlan,f.at(),f.in.conditions).state==S::NoPositiveCandidate);
        CHECK(!use.snapshot().point&&!use.snapshot().hostWaitMs&&use.snapshot().validUntilMonoMs==f.in.nowMonoMs+5000);
        CHECK(use.check(f.in.currentPlan,f.at(5000),f.in.conditions).state==S::Expired);}
    {Fixture f;prices(f.in,"0.005");auto study=f.build();EconomicStudyUse use(study,f.policy,f.intent,f.token);f.policy.maximumResultAgeMs=1;
        CHECK(use.check(f.in.currentPlan,f.at(2),f.in.conditions).point==1&&use.snapshot().hostWaitMs==10);
        prices(f.in,"0.001");CHECK(use.check(f.in.currentPlan,f.at(3),f.in.conditions).point==1); // external drafts cannot mutate the retained immutable study
        CHECK(study->summary().bestPoint==1&&use.snapshot().validUntilMonoMs==f.in.nowMonoMs+5000);}
}
}
int main(){basic();originalBounds();changedConditions();clocksAndNoCandidate();return finishTests("economic study validity");}
