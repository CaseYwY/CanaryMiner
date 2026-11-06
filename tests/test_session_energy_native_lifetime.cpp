// Pure synthetic observations over the actual session energy thread. No sensor, storage, helper or mining actions.
#include "product/energy_owner.h"
#include "product/native_process_admission.h"
#include "product_test_util.h"
#include <atomic>
#include <chrono>
#include <thread>
namespace {
using namespace om;
constexpr uint64_t T=1800000000000ULL,M=1000;
std::atomic<bool> failAllocations{false};std::atomic<unsigned> rejectedAllocations{0};
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
template<class P>bool until(P p){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(!p()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return p();}
}
bool canaryNativeAllocationFault(){if(!failAllocations)return false;++rejectedAllocations;return true;}
int main(int argc,char **argv){
    if(argc!=2)return 4;
    const std::string action=argv[1];if(action!="scope"&&action!="ready"&&action!="allocation"&&action!="start-allocation"&&action!="stop-allocation"&&action!="stopped-fault"&&action!="foreign"&&action!="restart")return 4;
    const auto original=nativeProcessEpoch();ElectricityPreferences p;p.collectEnergy=true;
    if(action=="scope"||action=="foreign")fault();
    auto owner=makeSessionEnergyOwner();
    if(action=="start-allocation"){failAllocations=true;CHECK(!owner->start(1,T,M,1,p,PowerOrigin::SyntheticFixture));failAllocations=false;
        CHECK(rejectedAllocations>0&&owner->snapshot()->state!=SessionEnergyState::Running);CHECK(owner->start(2,T,M,1,p,PowerOrigin::SyntheticFixture));}
    else if(action=="scope"){{NativeProcessScope old(original);CHECK(!owner->start(1,T,M,1,p,PowerOrigin::SyntheticFixture));}owner->stop();CHECK(!owner->start(2,T,M,1,p,PowerOrigin::SyntheticFixture));}
    else{
        CHECK(owner->start(1,T,M,1,p,PowerOrigin::SyntheticFixture));CHECK(owner->submit(1,{0,T,M,1000})==EnergyAdmission::Queued);
        CHECK(owner->submit(1,{0,T+1000,M+1000,1000})==EnergyAdmission::Queued);CHECK(until([&]{return owner->snapshot()->coveredDeviceMs==1000;}));
        const auto before=owner->snapshot();CHECK(before->state==SessionEnergyState::Running&&before->knownEnergyNumerator=="1000");
        if(action=="restart"){owner->stop();CHECK(owner->start(2,T,M,1,p,PowerOrigin::SyntheticFixture));CHECK(owner->snapshot()->state==SessionEnergyState::Running);}
        else{
            if(action=="foreign"){{NativeProcessScope old(original);CHECK(owner->snapshot()->state!=SessionEnergyState::Running);CHECK(owner->submit(1,{0,T+2000,M+2000,1000})==EnergyAdmission::Stopped);}CHECK(owner->snapshot()->state==SessionEnergyState::Running);}
            if(action=="stopped-fault")owner->requestStop();
            failAllocations=action=="allocation"||action=="stop-allocation";
            if(action=="stop-allocation")owner->requestStop();else fault();
            const auto revoked=owner->snapshot();CHECK(revoked->state!=SessionEnergyState::Running);CHECK(owner->submit(1,{0,T+3000,M+3000,1000})==EnergyAdmission::Stopped);
            failAllocations=false;
            if(action=="allocation"||action=="stop-allocation")CHECK(rejectedAllocations>0);
            else CHECK(revoked->state==(action=="stopped-fault"?SessionEnergyState::Stopped:SessionEnergyState::NativeFault)&&revoked->knownEnergyNumerator=="1000");
            CHECK(before->state==SessionEnergyState::Running&&before->knownEnergyNumerator=="1000");owner->stop();
            if(action!="stop-allocation")CHECK(!owner->start(2,T,M,1,p,PowerOrigin::SyntheticFixture));
        }
    }
    owner->stop();CHECK(!nativeProcessUnresolved());return finishTests("session energy original native lifetime and allocation-safe current revocation (synthetic CPU only)");
}
