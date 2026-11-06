// Synthetic observations only; no source/helper/device/game or worker executes.
#include "product/core_thermal_guard.h"
#include "product_test_util.h"
#include <atomic>
#include <new>
namespace {
using namespace om;using S=CoreThermalState;
std::atomic<bool> rejectAllocations{false};std::atomic<unsigned> allocationAttempts{0};
constexpr int64_t utc=1793145600000LL;constexpr uint64_t mono=10000;
const std::string device="GPU-00112233-4455-6677-8899-aabbccddee01";
GameGuardTime at(uint64_t offset){return {utc+static_cast<int64_t>(offset),mono+offset};}
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
CoreThermalPolicy policy(bool extended){CoreThermalPolicy p;p.enabled=true;p.maximumAgeMs=p.maximumGapMs=1000;p.coolingMs=500;p.devices={{device,80,65}};
    if(extended){p.profile=SafetyTelemetryProfile::ExtendedFields;p.devices[0].memoryPauseAtC=90;p.devices[0].memoryResumeAtOrBelowC=70;}return p;}
SafetyTelemetrySnapshot sample(uint64_t revision,uint64_t offset,bool extended){SafetyTelemetrySnapshot s;
    s.state=SafetyTelemetryOwnerState::Ready;s.transport=LocalTaskStatus::Ok;s.status=SafetyTelemetryStatus::Ok;s.completed=revision;
    s.request=SafetyTelemetryRequest{true,1,revision,at(offset),500,{device}};
    s.reply=SafetyTelemetryReply{SafetyTelemetryStatus::Ok,at(offset+1),at(offset+11),{{40,0,511}}};
    if(extended){s.request->profile=SafetyTelemetryProfile::ExtendedFields;const auto stamp=s.reply->began.utcMs*1000;
        s.reply->readings[0].fields={{82,5,stamp,0,50},{193,5,stamp,0,-10},{194,5,stamp,0,0},{195,5,stamp,0,2},{196,5,stamp,0,3},{230,1,stamp,0,0}};}
    return s;}
}
bool canaryNativeAllocationFault(){if(!rejectAllocations.load())return false;++allocationAttempts;return true;}
int main(int argc,char **argv){if(argc!=3)return 4;
    const std::string kind=argv[1],action=argv[2];if((kind!="core"&&kind!="extended")||(action!="fault"&&action!="intent-stop"&&action!="foreign"&&action!="foreign-start"&&action!="allocation"))return 4;
    const bool extended=kind=="extended";const auto original=nativeProcessEpoch();if(action=="foreign"||action=="foreign-start")fault();
    SessionIntent intent;const auto token=intent.explicitStart();CoreThermalGuard guard(policy(extended),intent,token);
    if(action=="foreign-start"){{NativeProcessScope old(original);CHECK(!guard.start(at(0)));CHECK(guard.snapshot().state==S::Stopped);}
        CHECK(guard.snapshot().state==S::Dormant);}
    CHECK(guard.start(at(0)));CHECK(guard.update(at(20),sample(1,0,extended)).state==S::Cooling);
    const auto source=sample(2,510,extended);const auto passing=extended?S::ExtendedWithinLimits:S::CoreWithinLimits;
    CHECK(guard.update(at(530),source).state==passing);const auto historical=guard.snapshot();CHECK(historical.validUntilMonoMs>at(530).monotonicMs);
    if(action=="foreign"){{NativeProcessScope old(original);const auto denied=guard.snapshot();CHECK(denied.state==S::Stopped&&!denied.validUntilMonoMs&&!denied.sourceRevision&&!denied.observed);
        auto tainted=source;tainted.unreaped=true;tainted.transport=LocalTaskStatus::Unreaped;
        CHECK(guard.update(at(900000),tainted).state==S::Stopped);CHECK(!guard.start(at(900000)));}
        CHECK(guard.snapshot().state==passing&&guard.snapshot().validUntilMonoMs==historical.validUntilMonoMs&&!guard.snapshot().sourceUnreaped);
        CHECK(guard.update(at(600),source).state==passing);}
    if(action=="allocation"){bool injected=false;void *probe=nullptr;rejectAllocations=true;
        try{probe=::operator new(1);}catch(const std::bad_alloc &){injected=true;}
        rejectAllocations=false;::operator delete(probe);CHECK(injected&&allocationAttempts.load()>0);allocationAttempts=0;}
    if(action=="intent-stop")intent.stop();else fault();
    if(action=="allocation")rejectAllocations=true;
    bool withdrew=false,threw=false;try{const auto &stopped=guard.snapshot();withdrew=stopped.state==S::Stopped&&!stopped.validUntilMonoMs&&!stopped.coolSamples&&!stopped.coolingSince;}catch(...){threw=true;}
    rejectAllocations=false;CHECK(withdrew&&!threw);if(action=="allocation")CHECK(allocationAttempts.load()==0);
    CHECK(guard.snapshot().sourceRevision==historical.sourceRevision&&guard.snapshot().observed&&historical.observed&&guard.snapshot().observed->utcMs==historical.observed->utcMs);
    CHECK(historical.state==passing&&historical.validUntilMonoMs>at(530).monotonicMs);
    auto late=source;late.unreaped=true;late.transport=LocalTaskStatus::Unreaped;
    CHECK(guard.update(at(700),late).state==S::Stopped&&guard.snapshot().sourceUnreaped&&!guard.snapshot().validUntilMonoMs);
    CHECK(!guard.start(at(900))&&guard.update(at(900),source).state==S::Stopped);
    return finishTests("thermal current original-parent/stale caller and allocation refusal");
}
