// Cross-platform owner boundaries. The explicitly absent helper cannot run a child or access a store.
#include "product/native_process_admission.h"
#include "product/energy_authorization_owner.h"
#include "product/energy_leased_capture.h"
#include "product/energy_runtime.h"
#include "energy_history_fixture.h"
#include "product_test_util.h"
#include <filesystem>
namespace {
using namespace om;
constexpr auto T=energy_history_fixture::T;
const std::vector<std::string> devices={"GPU-aaaaaaaa-0000-0000-0000-000000000001"};
void attempt(const std::filesystem::path &helper,const std::filesystem::path &file,bool allowed){
    {EnergyHistoryOwner owner(helper,file,energy_history_fixture::binding(),{},100,[]{return T+20000;});CHECK(owner.start(1)==allowed);owner.stop();}
    {EnergyAuthorizationOwner owner(helper,file,false,100,[]{return T+20000;});CHECK(owner.start(1)==allowed);owner.stop();}
    {auto owner=makeLeasedEnergyCapture(helper,helper,file,false,100,[]{return T+20000;});CHECK(owner->start(1,devices)==allowed);owner->stop();}
    {EnergyCapturePlan plan;plan.privateNamespace=std::string(32,'1');plan.sessionId="fixture.memory-scope";plan.policy.persistenceAuthorized=true;
        const std::vector<std::string> ids={"fixture.board"};CHECK(makeEnergyCaptureBinding(plan.privateNamespace,T,ids,PowerOrigin::SyntheticFixture,&plan.binding));
        auto owner=makeEnergyCapture(helper,file,plan,100,[]{return T+20000;});CHECK(owner->start(1,ids)==allowed);owner->stop();}
    {auto owner=makeEnergyRuntime(helper,helper,file,true,false,100,[]{return T+20000;});CHECK(owner->start(1,devices)==allowed);owner->stop();}
}
}
int main(){
    const auto root=std::filesystem::path(makeScratchDir("energy-original-scope")),helper=root/"explicitly-absent-helper",file=root/"never-created.db";
    CHECK(!std::filesystem::exists(helper));const auto original=nativeProcessEpoch();NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();
    {NativeProcessScope old(original);attempt(helper,file,false);}
    attempt(helper,file,true);CHECK(!std::filesystem::exists(file)&&!nativeProcessUnresolved());
    std::filesystem::remove(root);return finishTests("five energy owner original caller scopes and fresh explicit controls (no helper executable)");
}
