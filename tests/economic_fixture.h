// Synthetic offline fixtures only; the inputs deliberately have no real device/account/source mapping.
#pragma once
#include "product/economic_study.h"
#include "credit_fixture.h"
#include "electricity_fixture.h"
#include <algorithm>
#include <stdexcept>
namespace economic_fixture {
using namespace om;using electricity_fixture::d;
constexpr int64_t T=electricity_fixture::T;
inline std::shared_ptr<const WorkProfile> profile(const std::string &work0="200",const std::string &work1="160",unsigned ms0=2000,unsigned ms1=2000,uint32_t mw0=200000,uint32_t mw1=100000){
    WorkProfileInput in;in.generation=7;in.counterEpoch=1;auto &p=in.plan;p.enabled=true;p.qualification=WorkProfileQualification::SyntheticFixture;p.key="fixture.economic-plan";
    p.identity={"fixture.device","fixture.platform","fixture.software","fixture.driver","fixture.kernel","fixture.work","fixture.environment",{PowerBasis::GpuBoard,PowerOrigin::SyntheticFixture,"fixture.board","fixture.power"}};
    p.hostWaitMs={0,10};p.repetitions=1;p.telemetryCadenceMs=1;p.maximumSampleGapMs=60000;p.measurementMs=std::min(ms0,ms1);p.maximumElapsedMs=86400000;p.validForMs=86400000;p.maximumEnergy.numerator=d("1000000000000");
    const uint64_t total=static_cast<uint64_t>(ms0)+ms1+1000;const auto fp=workProfileFingerprint(p.identity);ExactDecimal all;d(work0).add(d(work1),&all);
    const uint64_t ticks[]={0,ms0,ms0+1000ULL,total};
    for(size_t i=0;i<4;++i){WorkProfileSample s;s.generation=7;s.counterEpoch=1;s.utcMs=T-static_cast<int64_t>(total)+static_cast<int64_t>(ticks[i]);s.monotonicMs=1000+ticks[i];s.fingerprint=fp;
        s.appliedHostWaitMs=i<2?0:10;s.completeWorkBoundary=s.safetyTelemetryValid=s.thermalSafe=s.gameStateKnown=true;s.milliwatts=i<2?mw0:mw1;
        s.userTmacs=i==0?d("0"):i<3?d(work0):all;s.donationTmacs=i==0?d("0"):i<3?d("20"):d("40");in.samples.push_back(s);}
    in.windows={{0,0,0,0,1},{1,0,2,2,3}};auto out=buildWorkProfile(in);if(!out)throw std::runtime_error("invalid synthetic work");return out;
}
inline RewardCalibrationInput calibration(const EconomicInput &in,const std::string &tokens="1"){
    RewardCalibrationInput c;c.qualification=ForecastQualification::SyntheticFixture;c.binding=in.production;c.modelKey="fixture.reward";c.profileKey=economicProfileKey(*in.profile);
    c.earningAlignmentContract="fixture.earning-alignment";c.calibratedUtcMs=in.nowUtcMs;c.validUntilUtcMs=in.nowUtcMs+86400000;
    RewardPeriod period;period.beginUtcMs=T-20000;period.endUtcMs=T-10000;period.credits.binding=in.production;period.credits.sequence=1;period.credits.snapshotId="fixture.committed";
    auto &t=period.credits.totals;t.status=CreditLedgerStatus::Ok;t.completeness=CreditCompleteness::Complete;t.qualification=CreditQualification::SyntheticFixture;t.scope=CreditScope::Worker;
    t.fromExclusiveMs=period.beginUtcMs-1;t.throughInclusiveMs=period.endUtcMs-1;t.sourceAsOfMs=t.observedAtMs=in.nowUtcMs;t.requestedMs=t.coveredMs=10000;t.matchedRecords=1;
    t.knownPostedNet=d(tokens);t.completePostedNet=t.knownPostedNet;
    period.work={ForecastQualification::SyntheticFixture,in.production,c.profileKey,{{period.beginUtcMs,period.endUtcMs,ForecastWorkState::UserMining,d("1000")}}};c.periods={period};return c;
}
inline void reward(EconomicInput &in,const std::string &tokens="1"){in.reward=calibrateReward(calibration(in,tokens));if(!in.reward)throw std::runtime_error("invalid synthetic reward");}
inline void prices(EconomicInput &in,const std::string &token="0.01",const std::string &fx="1"){
    const auto end=static_cast<uint64_t>(in.nowUtcMs/60000)*60;MarketCandles p,f;
    p.observation={MarketSeries::GatePearlMainnetUsdtV1,MarketOrigin::SyntheticFixture,static_cast<uint64_t>(in.nowUtcMs/1000),static_cast<uint64_t>(in.nowUtcMs/1000),in.nowMonoMs,in.generation};
    f.observation=p.observation;f.observation.series=MarketSeries::KrakenUsdtUsdV1;p.convention=f.convention=MarketMinuteConvention::FixtureMinuteStart;
    for(unsigned i=0;i<in.policy.lookbackMinutes;++i){const auto at=end-(in.policy.lookbackMinutes-i)*60;
        p.minutes.push_back({at,d(token),d(token),d(token),d(token),d("1"),true});f.minutes.push_back({at,d(fx),d(fx),d(fx),d(fx),d("1"),true});}
    in.pearl=std::move(p);in.fx=std::move(f);
}
inline EconomicInput input(std::shared_ptr<const WorkProfile> p=profile()){
    EconomicInput in;in.policy.enabled=true;in.generation=9;in.nowUtcMs=T;in.nowMonoMs=1000000;in.profile=std::move(p);in.currentPlan=in.profile->input().plan;
    in.conditions={true,true,true,false,false,false};in.production=credit_fixture::contract();in.production.historyStartMs=T-86400000;
    reward(in);prices(in);
    auto &e=in.electricity;e.configured=true;e.plan=electricity_fixture::plan(T+1,T+1+86400000);e.plan.currency="USD";e.plan.revisions[0].baseRatePerKwh=d("0.5");
    e.opening=electricity_fixture::opening(T+1,T+1+86400000);e.opening.evidence=in.currentPlan.identity.power;
    in.candidates.resize(2);for(size_t i=0;i<2;++i){auto &a=in.candidates[i];a.permitted=a.declared=true;a.key="fixture.transition."+std::to_string(i);a.observedUtcMs=T;}
    return in;
}
} // namespace economic_fixture
