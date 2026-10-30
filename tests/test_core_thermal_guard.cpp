// Synthetic temperatures/clocks only. These example thresholds are not product defaults or hardware qualification.
#include "product/core_thermal_guard.h"
#include "product_test_util.h"
#include <thread>
using namespace om;
namespace {
using S=CoreThermalState;using O=SafetyTelemetryOwnerState;using R=SafetyTelemetryStatus;using T=LocalTaskStatus;
constexpr int64_t utc=1793145600000LL;constexpr uint64_t mono=10000;
const std::string id1="GPU-00112233-4455-6677-8899-aabbccddee01",id2="GPU-00112233-4455-6677-8899-aabbccddee02";
GameGuardTime at(uint64_t offset){return {utc+static_cast<int64_t>(offset),mono+offset};}
CoreThermalPolicy policy(){CoreThermalPolicy p;p.enabled=true;p.maximumAgeMs=p.maximumGapMs=1000;p.coolingMs=500;p.devices={{id1,80,65}};return p;}
SafetyTelemetrySnapshot sample(uint64_t revision,uint64_t offset,int degrees=40){
    SafetyTelemetrySnapshot s;s.state=O::Ready;s.transport=T::Ok;s.status=R::Ok;s.completed=revision;
    s.request=SafetyTelemetryRequest{true,1,revision,at(offset),500,{id1}};
    s.reply=SafetyTelemetryReply{R::Ok,at(offset+1),at(offset+11),{{degrees,0,511}}};return s;
}
struct Fixture {
    SessionIntent intent;SessionToken token=intent.explicitStart();CoreThermalGuard guard;
    explicit Fixture(CoreThermalPolicy p=policy()):guard(std::move(p),intent,token){CHECK(guard.start(at(0)));}
    const CoreThermalSnapshot &feed(const SafetyTelemetrySnapshot &s,uint64_t now){return guard.update(at(now),s);}
    void ready(){CHECK(feed(sample(1,0),20).state==S::Cooling);CHECK(feed(sample(2,510),530).state==S::CoreWithinLimits);}
};
void policies(){
    const auto p=policy();CHECK(validCoreThermalPolicy(p));
    for(const auto &change:std::vector<std::function<void(CoreThermalPolicy &)>>{
        [](auto &v){v.devices.clear();},[](auto &v){v.devices.resize(65,v.devices[0]);},[](auto &v){v.devices.push_back(v.devices[0]);},
        [](auto &v){v.devices[0].uuid="GPU-00000000-0000-0000-0000-000000000000";},[](auto &v){v.devices[0].pauseAtC=251;},
        [](auto &v){v.devices[0].resumeAtOrBelowC=v.devices[0].pauseAtC;},[](auto &v){v.devices[0].resumeAtOrBelowC=-274;},
        [](auto &v){v.maximumAgeMs=99;},[](auto &v){v.maximumAgeMs=5001;},[](auto &v){v.maximumGapMs=99;},[](auto &v){v.maximumGapMs=1001;},
        [](auto &v){v.coolingMs=99;},[](auto &v){v.coolingMs=600001;},[](auto &v){v.minimumCoolSamples=1;},[](auto &v){v.minimumCoolSamples=10001;}}){
        auto bad=p;change(bad);CHECK(!validCoreThermalPolicy(bad));SessionIntent i;auto t=i.explicitStart();CoreThermalGuard g(bad,i,t);
        CHECK(!g.start(at(0))&&g.snapshot().state==S::InvalidPolicy&&!g.start(at(10)));}
    SessionIntent i;auto token=i.explicitStart();CoreThermalGuard off({},i,token);CHECK(!off.start(at(0))&&off.snapshot().state==S::Disabled);
    CoreThermalGuard idle(p,i,token);CHECK(idle.update(at(20),sample(1,0)).state==S::Dormant);idle.requestStop();CHECK(!idle.start(at(0))&&idle.snapshot().state==S::Stopped);
    CoreThermalGuard invalid(p,i,token);CHECK(!invalid.start({-1,1})&&invalid.snapshot().state==S::ClockFault);
    CoreThermalGuard foreign(p,i,{2});CHECK(!foreign.start(at(0))&&foreign.snapshot().state==S::Stopped);
    auto copied=p;CoreThermalGuard frozen(copied,i,token);copied.devices[0].pauseAtC=1;CHECK(frozen.start(at(0)));
    CHECK(frozen.update(at(20),sample(1,0)).coolSamples==1);CHECK(frozen.update(at(530),sample(2,510)).state==S::CoreWithinLimits);
}
void cooling(){
    {Fixture f;const auto first=sample(1,0);CHECK(f.feed(first,20).coolSamples==1);
        CHECK(f.feed(first,900).state==S::Cooling&&f.guard.snapshot().coolSamples==1);
        CHECK(f.feed(first,1001).state==S::Unavailable&&f.guard.snapshot().coolSamples==0);}
    {Fixture f;CHECK(f.feed(sample(1,0),20).state==S::Cooling);
        CHECK(f.feed(sample(2,509),529).state==S::Cooling);CHECK(f.feed(sample(3,510),530).state==S::ClockFault);}
    {Fixture f;f.ready();const auto deadline=f.guard.snapshot().validUntilMonoMs;CHECK(deadline==mono+1511);
        CHECK(f.feed(sample(2,510),1200).state==S::CoreWithinLimits&&f.guard.snapshot().validUntilMonoMs==deadline);
        CHECK(f.feed(sample(2,510),1511).state==S::Unavailable&&f.guard.snapshot().validUntilMonoMs==0);}
    {auto p=policy();p.minimumCoolSamples=3;Fixture f(p);CHECK(f.feed(sample(1,0),20).coolSamples==1);
        CHECK(f.feed(sample(2,510),530).state==S::Cooling);CHECK(f.feed(sample(3,700),720).state==S::CoreWithinLimits);}
    {Fixture f;f.ready();CHECK(f.feed(sample(3,700,79),720).state==S::CoreWithinLimits);
        CHECK(f.feed(sample(4,900,80),920).state==S::Cooling&&f.guard.snapshot().coolSamples==0);
        CHECK(f.feed(sample(5,1100,65),1120).coolSamples==1);
        CHECK(f.feed(sample(6,1300,66),1320).state==S::Cooling&&f.guard.snapshot().coolSamples==0);
        CHECK(f.feed(sample(7,1500,65),1520).coolSamples==1);
        CHECK(f.feed(sample(8,2010,65),2030).state==S::CoreWithinLimits);}
    {Fixture f;f.ready();CHECK(f.feed(sample(4,700),720).state==S::Cooling&&f.guard.snapshot().coolSamples==1);
        CHECK(f.feed(sample(5,1210),1230).state==S::CoreWithinLimits);}
    {Fixture f;f.ready();CHECK(f.feed(sample(3,1511),1531).state==S::Cooling&&f.guard.snapshot().coolSamples==1);}
    for(bool unavailable:{false,true}){Fixture f;f.ready();auto bad=sample(3,700);bad.state=O::Waiting;
        if(unavailable){bad.status=R::Unavailable;bad.reply->status=R::Unavailable;bad.reply->readings.clear();}
        else{bad.status=R::Unavailable;bad.transport=T::Timeout;bad.reply.reset();}
        CHECK(f.feed(bad,720).state==S::Unavailable&&f.guard.snapshot().validUntilMonoMs==0);
        CHECK(f.feed(sample(4,900),920).state==S::Cooling&&f.guard.snapshot().coolSamples==1);
        CHECK(f.feed(sample(5,1410),1430).state==S::CoreWithinLimits);}
    {auto p=policy();p.devices.push_back({id2,70,55});Fixture f(p);auto s=sample(1,0);s.request->devices.push_back(id2);s.reply->readings.push_back({56,0,511});
        CHECK(f.feed(s,20).state==S::Cooling&&f.guard.snapshot().coolSamples==0);
        s=sample(2,200);s.request->devices.push_back(id2);s.reply->readings.push_back({55,0,511});CHECK(f.feed(s,220).coolSamples==1);
        s=sample(3,710);s.request->devices.push_back(id2);s.reply->readings.push_back({55,0,511});CHECK(f.feed(s,730).state==S::CoreWithinLimits);
        s=sample(4,900);s.request->devices.push_back(id2);s.reply->readings.push_back({70,0,511});CHECK(f.feed(s,920).state==S::Cooling);}
}
void capabilities(){
    for(uint64_t bit:{8,32,64,128}){Fixture f;f.ready();auto s=sample(3,700);s.reply->readings[0].activeClockEvents=bit;
        CHECK(f.feed(s,720).state==S::Cooling&&f.guard.snapshot().validUntilMonoMs==0);}
    for(uint64_t bit:{8,32,64,128}){Fixture f;auto s=sample(1,0);s.reply->readings[0].supportedClockEvents&=~bit;
        CHECK(f.feed(s,20).state==S::Unsupported);}
    {Fixture f;auto s=sample(1,0);s.reply->readings[0].supportedClockEvents=1023;s.reply->readings[0].activeClockEvents=512;
        CHECK(f.feed(s,20).state==S::Unsupported);}
    for(uint64_t bit:{0,1,2,4,16,256}){Fixture f;auto s=sample(1,0);s.reply->readings[0].supportedClockEvents=UINT64_MAX;s.reply->readings[0].activeClockEvents=bit;
        CHECK(f.feed(s,20).state==S::Cooling);s=sample(2,510);s.reply->readings[0].supportedClockEvents=UINT64_MAX;s.reply->readings[0].activeClockEvents=bit;
        CHECK(f.feed(s,530).state==S::CoreWithinLimits);}
    {Fixture f;f.ready();auto s=sample(3,700);s.reply->readings[0].supportedClockEvents=1023;CHECK(f.feed(s,720).state==S::Faulted);
        CHECK(f.feed(sample(4,900),920).state==S::Faulted);}
}
void bindingAndStop(){
    for(const auto &change:std::vector<std::function<void(SafetyTelemetrySnapshot &)>>{
        [](auto &s){s.request->epoch=2;},[](auto &s){s.request->devices[0]=id2;},[](auto &s){s.request->collectionBudgetMs=499;},
        [](auto &s){s.request->enabled=false;},[](auto &s){s.reply->readings.clear();},[](auto &s){s.completed=99;},
        [](auto &s){s.transport=T::Timeout;},[](auto &s){s.state=O::Waiting;},[](auto &s){s.status=R::Unavailable;}}){
        Fixture f;f.ready();auto s=sample(3,700);change(s);CHECK(f.feed(s,720).state==S::Faulted&&f.guard.snapshot().validUntilMonoMs==0);
        CHECK(f.feed(sample(4,900),920).state==S::Faulted&&!f.guard.start(at(1000)));}
    {Fixture f;f.ready();auto s=sample(2,510);s.reply->readings[0].temperatureC=41;CHECK(f.feed(s,700).state==S::Faulted);}
    {Fixture f;f.ready();CHECK(f.feed(sample(1,0),700).state==S::Faulted);}
    {Fixture f;f.ready();auto s=sample(2,510);s.pending=true;CHECK(f.feed(s,700).state==S::CoreWithinLimits);}
    for(auto state:{O::Faulted,O::Stopped,O::ClockFault}){Fixture f;f.ready();SafetyTelemetrySnapshot s;s.state=state;
        CHECK(f.feed(s,700).state==(state==O::ClockFault?S::ClockFault:S::Faulted));}
    for(auto status:{R::DeviceFault,R::Identity,R::Invalid,R::Clock}){Fixture f;auto s=sample(1,0);s.state=O::Waiting;s.status=s.reply->status=status;s.reply->readings.clear();
        CHECK(f.feed(s,20).state==(status==R::Clock?S::ClockFault:S::Faulted));}
    {Fixture f;f.ready();f.intent.stop();CHECK(f.feed(sample(3,700),720).state==S::Stopped);
        CHECK(f.intent.explicitStart().epoch==2);CHECK(f.feed(sample(4,900),920).state==S::Stopped&&!f.guard.start(at(1000)));}
    {Fixture f;f.ready();f.guard.requestStop();auto s=sample(3,700);s.unreaped=true;s.transport=T::Unreaped;
        CHECK(f.feed(s,720).state==S::Stopped&&f.guard.snapshot().sourceUnreaped&&f.guard.snapshot().validUntilMonoMs==0);}
    {Fixture f;f.ready();auto s=sample(3,700);s.unreaped=true;s.transport=T::Unreaped;CHECK(f.feed(s,720).state==S::Faulted&&f.guard.snapshot().sourceUnreaped);}
    {Fixture f;f.guard.requestStop();SafetyTelemetrySnapshot s;s.transport=T::Unreaped;
        CHECK(f.feed(s,20).state==S::Stopped&&f.guard.snapshot().sourceUnreaped);}
}
void clocks(){
    {Fixture f;f.ready();CHECK(f.feed(sample(2,510),529).state==S::ClockFault);}
    {Fixture f;f.ready();CHECK(f.guard.update({utc+3000,mono+600},sample(2,510)).state==S::ClockFault);}
    {Fixture f;auto s=sample(1,100);CHECK(f.feed(s,90).state==S::ClockFault);}
    {Fixture f;auto s=sample(1,0);s.reply->finished.utcMs-=50;s.reply->status=s.status=R::Unavailable;s.reply->readings.clear();s.state=O::Waiting;
        CHECK(f.feed(s,20).state==S::ClockFault);}
    {Fixture f;auto s=sample(1,0);s.state=O::Waiting;s.reply->status=s.status=R::Unavailable;s.reply->readings.clear();
        CHECK(f.feed(s,20).state==S::Unavailable);CHECK(f.feed(sample(2,5),30).state==S::ClockFault);}
    {Fixture f;f.ready();const auto s=sample(2,510);auto t=at(700);t.utcMs+=100;
        CHECK(f.guard.update(t,s).state==S::CoreWithinLimits);const auto clipped=f.guard.snapshot().validUntilMonoMs;CHECK(clipped==mono+1411);
        CHECK(f.feed(s,900).state==S::CoreWithinLimits&&f.guard.snapshot().validUntilMonoMs==clipped);
        CHECK(f.feed(s,1411).state==S::Unavailable);}
    {SessionIntent intent;const auto token=intent.explicitStart();CoreThermalGuard guard(policy(),intent,token);auto base=at(0);base.monotonicMs=UINT64_MAX-100;
        CHECK(guard.start(base));auto s=sample(1,0);s.request->issued.monotonicMs=UINT64_MAX-100;s.reply->began.monotonicMs=UINT64_MAX-99;s.reply->finished.monotonicMs=UINT64_MAX-89;
        auto now=at(20);now.monotonicMs=UINT64_MAX-80;CHECK(guard.update(now,s).state==S::ClockFault);}
}
void nativeOwner(const std::filesystem::path &fixture){
    SessionIntent intent;const auto token=intent.explicitStart();auto p=policy();p.maximumAgeMs=p.maximumGapMs=2000;p.coolingMs=100;
    CoreThermalGuard guard(p,intent,token);SafetyTelemetryPolicy collection;collection.enabled=true;collection.pollMs=100;
    SafetyTelemetryOwner owner(fixture,{id1},collection);CHECK(guard.start(safetyTelemetryTime())&&owner.start(token.epoch));
    const auto deadline=safetyTelemetryTime().monotonicMs+4000;
    do{const auto observed=owner.snapshot();guard.update(safetyTelemetryTime(),*observed);
        if(guard.snapshot().state==S::CoreWithinLimits)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }while(safetyTelemetryTime().monotonicMs<deadline);
    const auto accepted=guard.snapshot();CHECK(accepted.state==S::CoreWithinLimits&&accepted.sourceRevision>=2&&accepted.observed&&accepted.validUntilMonoMs>safetyTelemetryTime().monotonicMs);
    owner.requestStop();CHECK(guard.update(safetyTelemetryTime(),*owner.snapshot()).state==S::Faulted&&guard.snapshot().validUntilMonoMs==0);
    owner.stop();CHECK(!owner.start(token.epoch)&&!guard.start(safetyTelemetryTime()));
}
}
int main(int argc,char **argv){if(argc!=2)return 2;
    policies();cooling();capabilities();bindingAndStop();clocks();nativeOwner(std::filesystem::absolute(argv[1]));return finishTests("core thermal hysteresis");}
