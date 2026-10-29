#include "product/work_profile.h"
#include "product_test_util.h"
#include <functional>
#include <limits>
using namespace om;
namespace {
using S=WorkProfileStatus;
constexpr int64_t T=1767225600000LL;
ExactDecimal d(const std::string &s){ExactDecimal value;CHECK(ExactDecimal::parse(s,&value));return value;}
WorkProfileInput fixture(){
    WorkProfileInput in;auto &p=in.plan;p.enabled=true;p.qualification=WorkProfileQualification::SyntheticFixture;p.key="fixture.profile";
    p.identity={"fixture.device","fixture.platform","fixture.software","fixture.driver","fixture.kernel","fixture.work-shape","fixture.environment",{PowerBasis::GpuBoard,PowerOrigin::SyntheticFixture,"fixture.scope","fixture.sensor"}};
    p.hostWaitMs={0,10};p.repetitions=2;p.telemetryCadenceMs=100;p.maximumSampleGapMs=200;p.warmupMs=100;p.measurementMs=200;p.maximumElapsedMs=2000;p.validForMs=1000;p.maximumEnergy.numerator=d("14000");
    in.generation=7;in.counterEpoch=8;const unsigned user[]={0,10,20,30,40,70,100,130,140,150,155,160,170,175,180};
    const auto fingerprint=workProfileFingerprint(p.identity);
    for(unsigned n=0;n<15;++n){WorkProfileSample s;s.generation=7;s.counterEpoch=8;s.monotonicMs=5000+n*100;s.utcMs=T+n*100;s.fingerprint=fingerprint;
        s.appliedHostWaitMs=n<8?0:10;s.completeWorkBoundary=s.safetyTelemetryValid=s.thermalSafe=s.gameStateKnown=true;s.milliwatts=10000;s.userTmacs=d(std::to_string(user[n]));s.donationTmacs=d(std::to_string(n));in.samples.push_back(s);}
    in.windows={{0,0,0,1,3},{0,1,3,4,7},{1,0,8,9,11},{1,1,11,12,14}};return in;
}
std::string rate(WorkProfileRate r){std::string out;CHECK(r.formatPerSecond(6,&out));return out;}
void reject(const WorkProfileInput &base,S status,const std::function<void(WorkProfileInput &)> &change){auto in=base;change(in);S actual=S::Ok;CHECK(!buildWorkProfile(in,&actual)&&actual==status);}
}
int main(){
    const auto in=fixture();S status=S::Invalid;const auto profile=buildWorkProfile(in,&status);CHECK(profile&&status==S::Ok);if(!profile)return finishTests("work profile");const auto &s=profile->summary();
    // Independent hand arithmetic: four windows total 900 ms; warm-ups and the switch account for 500 ms.
    CHECK(s.elapsedMs==1400&&s.measuredMs==900&&s.overheadMs==500&&s.beginUtcMs==T&&s.endUtcMs==T+1400);
    CHECK(s.totalEnergy.numerator.text()=="14000"&&s.measuredEnergy.numerator.text()=="9000"&&s.overheadEnergy.numerator.text()=="5000");
    CHECK(s.totalUserTmacs.text()=="180"&&s.totalDonationTmacs.text()=="14"&&s.overheadUserTmacs.text()=="50"&&s.overheadDonationTmacs.text()=="5");
    CHECK(s.completeCoverage&&!s.statisticalConfidenceAvailable&&s.sampleCount==15&&s.trials.size()==4&&s.points.size()==2);
    const auto &fast=s.points[0],&slow=s.points[1];CHECK(fast.hostWaitMs==0&&fast.repetitions==2&&fast.measuredMs==500&&fast.userTmacs.text()=="110"&&fast.donationTmacs.text()=="5");
    CHECK(rate(fast.meanUserRate())=="220.000000"&&rate(fast.minimumUserRate)=="100.000000"&&rate(fast.maximumUserRate)=="300.000000");
    CHECK(slow.hostWaitMs==10&&slow.measuredMs==400&&slow.energy.numerator.text()=="4000"&&rate(slow.meanUserRate())=="50.000000");
    CHECK(s.trials[0].warmupMs==100&&s.trials[1].elapsedMs==300&&s.trials[1].beginUtcMs==T+400&&s.trials[1].endUtcMs==T+700);
    std::string empty;CHECK(!WorkProfileRate{}.formatPerSecond(2,&empty));
    reject(in,S::Disabled,[](auto &x){x.plan.enabled=false;});reject(in,S::Unsupported,[](auto &x){x.plan.qualification=WorkProfileQualification::Unsupported;});
    reject(in,S::Unsupported,[](auto &x){x.plan.identity.power.origin=PowerOrigin::ObservedTelemetry;});
    for(auto change:std::vector<std::function<void(WorkProfileInput &)>>{
        [](auto &x){x.plan.key="imported.model-guess";},[](auto &x){x.plan.key=std::string(257,'x');},[](auto &x){x.plan.identity.deviceKey.clear();},
        [](auto &x){x.plan.hostWaitMs={0,0};},[](auto &x){x.plan.hostWaitMs={0,1001};},[](auto &x){x.plan.hostWaitMs.clear();},[](auto &x){x.plan.hostWaitMs.resize(33);},
        [](auto &x){x.plan.repetitions=0;},[](auto &x){x.plan.repetitions=33;},[](auto &x){x.plan.telemetryCadenceMs=0;},[](auto &x){x.plan.telemetryCadenceMs=60001;},
        [](auto &x){x.plan.maximumSampleGapMs=99;},[](auto &x){x.plan.maximumSampleGapMs=60001;},[](auto &x){x.plan.warmupMs=3600001;},
        [](auto &x){x.plan.measurementMs=99;},[](auto &x){x.plan.measurementMs=3600001;},[](auto &x){x.plan.maximumElapsedMs=86400001;},
        [](auto &x){x.plan.maximumElapsedMs=0;},[](auto &x){x.plan.validForMs=0;},[](auto &x){x.plan.validForMs=2592000001ULL;},
        [](auto &x){x.plan.maximumEnergy.numerator={};},[](auto &x){x.plan.maximumEnergy.numerator=d("-1");},
        [](auto &x){x.generation=0;},[](auto &x){x.counterEpoch=0;},[](auto &x){x.windows.pop_back();},[](auto &x){x.samples.resize(1);}})reject(in,S::Invalid,change);
    reject(in,S::Budget,[](auto &x){x.plan.maximumElapsedMs=1199;}); // below the minimum planned work, before accepting any samples
    reject(in,S::Budget,[](auto &x){x.plan.maximumElapsedMs=1399;});reject(in,S::Budget,[](auto &x){x.plan.maximumEnergy.numerator=d("13999.999");});
    reject(in,S::Capacity,[](auto &x){x.samples.resize(8193);});
    for(auto change:std::vector<std::function<void(WorkProfileInput &)>>{
        [](auto &x){x.samples[3].milliwatts.reset();},[](auto &x){x.samples.back().milliwatts.reset();},[](auto &x){x.samples[3].userTmacs.reset();},
        [](auto &x){x.samples[3].donationTmacs.reset();},[](auto &x){x.samples[1].completeWorkBoundary=false;},[](auto &x){x.samples.front().completeWorkBoundary=false;},
        [](auto &x){x.windows[0].measurementSample=0;},[](auto &x){x.windows[0].endSample=2;},[](auto &x){x.plan.maximumSampleGapMs=100;x.windows[0].endSample=5;},
        [](auto &x){for(size_t i=1;i<x.samples.size();++i){x.samples[i].utcMs+=200;x.samples[i].monotonicMs+=200;}}})reject(in,S::Coverage,change);
    for(auto change:std::vector<std::function<void(WorkProfileInput &)>>{
        [](auto &x){x.samples[2].utcMs--;},[](auto &x){x.samples[2].utcMs=x.samples[1].utcMs;},[](auto &x){x.samples[2].monotonicMs=x.samples[1].monotonicMs;},
        [](auto &x){x.samples[0].utcMs=-1;},[](auto &x){x.samples.back().utcMs=253402300800000LL;}})reject(in,S::Clock,change);
    for(auto change:std::vector<std::function<void(WorkProfileInput &)>>{
        [](auto &x){x.samples[2].generation++;},[](auto &x){x.samples[2].counterEpoch++;},[](auto &x){x.samples[2].fingerprint=std::string(64,'f');},
        [](auto &x){x.samples[3].userTmacs=d("19");},[](auto &x){x.samples[3].donationTmacs=d("1");},[](auto &x){x.samples[9].appliedHostWaitMs=0;}})reject(in,S::Attribution,change);
    reject(in,S::Invalid,[](auto &x){x.samples[2].userTmacs=d("-1");});reject(in,S::Invalid,[](auto &x){x.samples[2].donationTmacs=d("0.0000000000001");});
    reject(in,S::Invalid,[](auto &x){x.samples[2].userTmacs=d(std::string(39,'9'));});
    for(auto change:std::vector<std::function<void(WorkProfileInput &)>>{
        [](auto &x){x.samples[4].fault=true;},[](auto &x){x.samples[4].thermalSafe=false;},[](auto &x){x.samples[4].safetyTelemetryValid=false;}})reject(in,S::Unsafe,change);
    reject(in,S::Game,[](auto &x){x.samples[4].gameActive=true;});reject(in,S::Game,[](auto &x){x.samples[4].gameStateKnown=false;});
    for(auto change:std::vector<std::function<void(WorkProfileInput &)>>{
        [](auto &x){x.windows[0].candidate=1;},[](auto &x){x.windows[1].repetition=0;},[](auto &x){x.windows[1].beginSample=2;},
        [](auto &x){x.windows[0].measurementSample=3;},[](auto &x){x.windows[3].endSample=15;}})reject(in,S::Invalid,change);
    std::atomic<bool> cancel{true};CHECK(!buildWorkProfile(in,&status,&cancel)&&status==S::Cancelled);
    {auto zero=in;for(auto &sample:zero.samples){sample.milliwatts=0;sample.userTmacs=sample.donationTmacs=ExactDecimal{};}const auto z=buildWorkProfile(zero,&status);
        CHECK(z&&status==S::Ok&&z->summary().completeCoverage&&z->summary().totalEnergy.numerator.zero()&&rate(z->summary().points[0].meanUserRate())=="0.000000");}
    // Monotonic absolute origin is irrelevant; only nonwrapping measured differences count.
    {auto shifted=in;for(auto &sample:shifted.samples)sample.monotonicMs=UINT64_MAX-1400+static_cast<uint64_t>(sample.utcMs-T);const auto shiftedProfile=buildWorkProfile(shifted,&status);
        CHECK(shiftedProfile&&shiftedProfile->summary().totalEnergy.numerator.compare(s.totalEnergy.numerator)==0&&shiftedProfile->summary().elapsedMs==1400);}
    // Explicit zero warm-up and the full candidate/repetition shape are supported without an implicit sweep.
    {auto many=in;auto &p=many.plan;p.hostWaitMs.clear();for(unsigned i=0;i<32;++i)p.hostWaitMs.push_back(i);p.repetitions=32;p.warmupMs=0;p.measurementMs=p.telemetryCadenceMs=p.maximumSampleGapMs=100;p.maximumElapsedMs=200000;p.maximumEnergy.numerator=d("2000000");
        many.samples.clear();many.windows.clear();uint64_t tick=0;
        auto sample=[&](unsigned wait){auto value=in.samples[0];value.utcMs=T+static_cast<int64_t>(tick);value.monotonicMs=tick;value.appliedHostWaitMs=wait;value.userTmacs=d(std::to_string(tick));value.donationTmacs=ExactDecimal{};many.samples.push_back(value);};
        for(unsigned c=0;c<32;++c){sample(c);for(unsigned r=0;r<32;++r){const auto begin=many.samples.size()-1;tick+=100;sample(c);many.windows.push_back({c,r,begin,begin,begin+1});}tick+=100;}
        const auto full=buildWorkProfile(many,&status);CHECK(full&&status==S::Ok&&full->summary().trials.size()==1024&&full->summary().points.size()==32&&full->summary().measuredMs==102400&&full->summary().overheadMs==3100);}
    auto changed=in;changed.plan.key="fixture.edited";changed.samples[0].fingerprint="changed";CHECK(profile->input().plan.key=="fixture.profile"&&profile->input().samples[0].fingerprint==in.samples[0].fingerprint);
    auto id=in.plan.identity;const auto fingerprint=workProfileFingerprint(id);id.deviceKey="ab";id.platformKey="c";const auto one=workProfileFingerprint(id);id.deviceKey="a";id.platformKey="bc";CHECK(one!=workProfileFingerprint(id));
    SessionIntent intent;WorkProfileConditions healthy{true,true,true,false,false,false};WorkProfileUse off(profile,intent,{});CHECK(off.check(in.plan,T+1400,healthy)==S::Stopped);
    const auto token=intent.explicitStart();CHECK(off.check(in.plan,T+1400,healthy)==S::Stopped);
    WorkProfileUse usable(profile,intent,token);CHECK(usable.check(in.plan,T+1400,healthy)==S::Ok);
    auto conditions=healthy;conditions.gameActive=true;CHECK(usable.check(in.plan,T+1500,conditions)==S::Game);conditions=healthy;conditions.gameStateKnown=false;CHECK(usable.check(in.plan,T+1500,conditions)==S::Game);
    conditions=healthy;conditions.thermalSafe=false;CHECK(usable.check(in.plan,T+1600,conditions)==S::Unsafe);CHECK(usable.check(in.plan,T+1700,healthy)==S::Ok);
    for(auto change:std::vector<std::function<void(WorkProfileIdentity &)>>{
        [](auto &v){v.deviceKey+=".other";},[](auto &v){v.platformKey+=".other";},[](auto &v){v.softwareVersion+=".other";},[](auto &v){v.driverVersion+=".other";},
        [](auto &v){v.kernelKey+=".other";},[](auto &v){v.workShapeKey+=".other";},[](auto &v){v.environmentKey+=".other";},[](auto &v){v.power.scope+=".other";},
        [](auto &v){v.power.adapter+=".other";},[](auto &v){v.power.basis=PowerBasis::WholeSystemWall;},[](auto &v){v.power.origin=PowerOrigin::DeclaredForecast;}}){
        id=in.plan.identity;change(id);CHECK(workProfileFingerprint(id)!=fingerprint);auto plan=in.plan;plan.identity=id;WorkProfileUse use(profile,intent,token);CHECK(use.check(plan,T+1400,healthy)==S::Attribution&&use.check(in.plan,T+1400,healthy)==S::Attribution);}
    for(auto change:std::vector<std::function<void(WorkProfilePlan &)>>{
        [](auto &p){p.key+=".other";},[](auto &p){p.hostWaitMs[1]++;},[](auto &p){p.telemetryCadenceMs++;},[](auto &p){p.maximumSampleGapMs++;},
        [](auto &p){p.warmupMs++;},[](auto &p){p.measurementMs++;},[](auto &p){p.repetitions++;},[](auto &p){p.maximumElapsedMs++;},
        [](auto &p){p.maximumEnergy.numerator=d("15000");},[](auto &p){p.validForMs++;}}){auto plan=in.plan;change(plan);WorkProfileUse use(profile,intent,token);
        CHECK(use.check(plan,T+1400,healthy)==S::Attribution&&use.check(in.plan,T+1400,healthy)==S::Attribution);}
    {WorkProfileUse use(profile,intent,token);auto plan=in.plan;plan.enabled=false;CHECK(use.check(plan,T+1400,healthy)==S::Disabled&&use.check(in.plan,T+1400,healthy)==S::Disabled);}
    {WorkProfileUse use(profile,intent,token);conditions=healthy;conditions.drift=true;CHECK(use.check(in.plan,T+1400,conditions)==S::Drift&&use.check(in.plan,T+1400,healthy)==S::Drift);}
    {WorkProfileUse use(profile,intent,token);conditions=healthy;conditions.fault=true;CHECK(use.check(in.plan,T+1400,conditions)==S::Unsafe&&use.check(in.plan,T+1400,healthy)==S::Unsafe);}
    {WorkProfileUse use(profile,intent,token);CHECK(use.check(in.plan,T+2400,healthy)==S::Ok&&use.check(in.plan,T+2401,healthy)==S::Stale&&use.check(in.plan,T+1400,healthy)==S::Stale);}
    {WorkProfileUse use(profile,intent,token);CHECK(use.check(in.plan,T+1500,healthy)==S::Ok&&use.check(in.plan,T+1499,healthy)==S::Clock&&use.check(in.plan,T+1500,healthy)==S::Clock);}
    intent.stop();CHECK(usable.check(in.plan,T+1800,healthy)==S::Stopped);intent.explicitStart();CHECK(usable.check(in.plan,T+1800,healthy)==S::Stopped);
    return finishTests("work profile");
}
