#include "energy_collector_fixture.h"
#include "product/energy_runtime_access.h"
#include "product/local_store_lease.h"
#include "product_test_util.h"
using namespace om;
using namespace energy_collector_fixture;
namespace {
using A=EnergyAuthorizationAction;
using H=EnergyHistoryAdmission;
using L=EnergyLeaseState;
bool ready(EnergyRuntime &r,int64_t now=T+10000){return until([&]{r.tick(now,0);const auto s=r.authorizationSnapshot();return !s->pending&&!s->startupPending;});}
bool collecting(EnergyRuntime &r){return until([&]{r.tick(T+10000,0);return r.snapshot()->state==EnergyCaptureState::Collecting;});}
H send(EnergyRuntime &r,EnergyAuthorizationCommand c){const auto s=r.authorizationSnapshot();return r.authorize(s->generation,s->revision,c);}
EnergyAuthorizationCommand policy(EnergyRuntime &r,bool enabled){EnergyAuthorizationCommand c;c.action=A::Save;const auto s=r.authorizationSnapshot();
    if(s->reply&&s->reply->document){c.policy=s->reply->document->policy;c.limits=s->reply->document->limits;}c.policy.persistenceAuthorized=enabled;return c;}
void stopped(EnergyRuntime &r,const std::filesystem::path &file){
    const auto start=std::chrono::steady_clock::now();r.requestStop();CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));
    const auto a=r.authorizationSnapshot();const auto c=r.snapshot();const auto before=bytes(file);const auto access=energyRuntimeAuthorization(r);const auto review=energyRuntimeReview(r);
    CHECK(access.submit(a->generation,a->revision,{})==H::NotReady&&access.acknowledge(a->generation,a->revision)==H::NotReady);
    CHECK(review.inspect(c->generation,1)==H::NotReady&&review.retry(c->generation,1)==H::NotReady);
    CHECK(!r.start(99,devices));CHECK(r.submit(c->generation,{0,T,0,100})==EnergyCaptureAdmission::Stopped);
    r.tick(T+20000,2000);r.markGap(c->generation);r.stop();
    CHECK(r.snapshot()==c&&r.authorizationSnapshot()==a&&bytes(file)==before);
}
int nativeProbe(){
    auto runtime=makeNativeEnergyRuntime("state",false,false);
    if(!runtime||!runtime->start(1,devices)||!ready(*runtime))return 2;
    const auto s=runtime->authorizationSnapshot();
    if(s->reply&&s->reply->status==EnergyAuthorizationStatus::Missing)std::puts("native sibling ready; persistence disabled");
    else if(s->state==EnergyAuthorizationOwnerState::Faulted)std::puts("native sibling unavailable");
    else return 3;
    runtime->stop();return 0;
}
}
int main(int argc,char **argv){
    if(argc==2&&std::string(argv[1])=="--native-probe")return nativeProbe();
    CHECK(argc==4);if(argc!=4)return finishTests("energy runtime");
    const auto authorization=std::filesystem::absolute(argv[1]),collector=std::filesystem::absolute(argv[2]),fixture=std::filesystem::absolute(argv[3]);
    const auto root=std::filesystem::path(makeScratchDir("energy-runtime"));EnergyAuthorizationProcessClient client(authorization);
    const auto exe=energyRuntimeExecutable();CHECK(localTaskPath(exe));
    const auto paths=energyRuntimePaths(exe,root);CHECK(paths&&paths->authorizationHelper.parent_path()==exe.parent_path()&&paths->authorizationFile==root/"energy-persistence.json");
    CHECK(!energyRuntimePaths("relative",root)&&!energyRuntimePaths(exe,"relative"));
    CHECK(!makeNativeEnergyRuntime("",true,false)&&!makeNativeEnergyRuntime(std::string("state\0hidden",12),true,false));
    const auto unicode=root/std::filesystem::u8path(u8"synthetic-\u80fd\u8017 space");const auto u=energyRuntimePaths(unicode/"test",unicode);
    CHECK(u&&u->collectorHelper.parent_path()==unicode&&u->authorizationFile.parent_path()==unicode);
    auto make=[&](const std::filesystem::path &file,bool mining=true,bool disabled=false){return makeEnergyRuntime(authorization,collector,file,mining,disabled,5000,[]{return T+10000;});};
    {const auto p=root/"prestop.json";auto r=make(p);r->requestStop();CHECK(!r->start(1,devices)&&!std::filesystem::exists(p));}
    {auto r=make(root/"invalid-generation.json");CHECK(!r->start(0,devices)&&!r->start(1,devices));}
    // Defaults and idle settings never create a store or reserve a sampling identity.
    {const auto p=root/"missing.json";auto r=make(p);CHECK(r->start(1,devices)&&ready(*r));
        CHECK(r->snapshot()->leaseState==L::Disabled&&r->authorizationSnapshot()->reply->status==EnergyAuthorizationStatus::Missing);
        CHECK(!std::filesystem::exists(p)&&!std::filesystem::exists(suffix(p,".collector.lock")));stopped(*r,p);}
    const auto p=root/"enabled.json";auto d=enroll(client,p);CHECK(d.document&&d.status==EnergyAuthorizationStatus::Ok);if(!d.document)return finishTests("energy runtime");
    for(unsigned n=0;n<2;++n){const auto before=bytes(p);auto r=make(p,n==1,n==1);CHECK(r->start(2+n,devices)&&ready(*r));
        CHECK(r->snapshot()->leaseState==L::Disabled&&r->authorizationSnapshot()->cliNoEnergy==(n==1));
        EnergyAuthorizationCommand c;c.action=A::Provision;c.devices=devices;CHECK(send(*r,c)==H::Invalid);
        CHECK(bytes(p)==before&&!std::filesystem::exists(suffix(p,".collector.lock")));stopped(*r,p);}
    // Native callback selection cannot be replaced with a different set, nor inferred from ordinal/all/duplicates.
    for(const auto &ids:std::vector<std::vector<std::string>>{{},{"all"},{"0"},{devices[0],devices[0]},{devices[0],"GPU-short"}}){
        const auto f=root/"selection.json";auto r=make(f,false);CHECK(r->start(7,ids)&&ready(*r)&&r->selectedDevices().empty());
        EnergyAuthorizationCommand c;c.action=A::Enroll;c.devices=devices;CHECK(send(*r,c)==H::Invalid&&!std::filesystem::exists(f));
        EnergyAuthorizationPanel panel;panel.open(r->authorizationSnapshot());CHECK(panel.render(120,0,ColorLevel::None,energyRuntimeAuthorization(*r)).find("Enrollment unavailable")!=std::string::npos);}
    {auto r=make(root/"spoof.json",false);CHECK(r->start(7,devices)&&ready(*r));EnergyAuthorizationCommand c;c.action=A::Enroll;c.devices={devices[0]};CHECK(send(*r,c)==H::Invalid);}
    // Post-Provision load sees the fresh CAS. Saved OFF/ON cannot alter or revive this parent's frozen plan.
    std::string firstSession;
    {auto r=make(p);CHECK(r->start(8,devices)&&ready(*r)&&collecting(*r));const auto a=r->authorizationSnapshot();
        CHECK(a->reply&&a->reply->document&&a->reply->document->lastSessionId!=""&&a->reply->token!=d.token);
        firstSession=a->reply->document->lastSessionId;
        const auto access=energyRuntimeAuthorization(*r);EnergyAuthorizationPanel panel;panel.open(a);const auto text=panel.render(120,0,ColorLevel::None,access);
        CHECK(text.find("frozen active plan")!=std::string::npos&&text.find(firstSession)==std::string::npos&&text.find(devices[0])==std::string::npos);
        CHECK(send(*r,policy(*r,false))==H::Queued&&ready(*r));CHECK(!r->authorizationSnapshot()->reply->document->policy.persistenceAuthorized);
        CHECK(r->snapshot()->state==EnergyCaptureState::Collecting&&localStoreLeaseHeld(suffix(p,".collector.lock"))==true);
        CHECK(r->authorize(a->generation,a->revision,{})==H::ReviewChanged);
        CHECK(r->submit(8,{0,T+10000,0,100000})==EnergyCaptureAdmission::Buffered);stopped(*r,p);}
    {auto r=make(p);CHECK(r->start(9,devices)&&ready(*r));CHECK(r->snapshot()->leaseState==L::Disabled);
        CHECK(send(*r,policy(*r,true))==H::Queued&&ready(*r));r->tick(T+10000,1000);
        CHECK(r->snapshot()->leaseState==L::Disabled&&r->authorizationSnapshot()->reply->document->lastSessionId==firstSession);stopped(*r,p);}
    {auto r=make(p);auto reversed=devices;std::reverse(reversed.begin(),reversed.end());CHECK(r->start(10,reversed)&&ready(*r)&&collecting(*r));
        CHECK(r->authorizationSnapshot()->reply->document->lastSessionId!=firstSession);stopped(*r,p);}
    {const auto before=bytes(p);auto r=make(p);CHECK(r->start(11,{devices[0]})&&ready(*r));CHECK(r->snapshot()->leaseState==L::Lost&&bytes(p)==before);}
    // Hold the handshake AFTER reservation, proving no pre-handshake settings mutation or stale load is admitted.
    {const auto f=root/"fixture-lease-no-ack.json";CHECK(enroll(client,f).document.has_value());
        auto r=makeEnergyRuntime(authorization,fixture,f,true,false,5000,[]{return T+10000;});CHECK(r->start(12,devices));
        CHECK(until([&]{return std::filesystem::exists(suffix(f,".collector.json"));}));r->tick(T+10000,0);
        CHECK(r->authorizationSnapshot()->startupPending&&r->authorizationSnapshot()->pending);
        const auto before=bytes(f);CHECK(send(*r,policy(*r,false))==H::NotReady&&bytes(f)==before);
        stopped(*r,f);CHECK(r->snapshot()->provisioningOutcomeUnknown&&r->snapshot()->provisionRequest&&localStoreLeaseHeld(suffix(f,".collector.lock"))==false);}
    // A missing/wrong helper and a corrupt store stay unavailable; no executable search or silent repair.
    for(const auto &helper:std::vector<std::filesystem::path>{root/"absent-helper",collector}){
        const auto f=root/"bad-helper.json";auto r=makeEnergyRuntime(helper,collector,f,true,false,500,[]{return T+10000;});
        CHECK(r->start(13,devices)&&ready(*r));CHECK(r->authorizationSnapshot()->state==EnergyAuthorizationOwnerState::Faulted&&!std::filesystem::exists(f));}
    {const auto before=bytes(p);auto r=makeEnergyRuntime(authorization,authorization,p,true,false,500,[]{return T+10000;});CHECK(r->start(14,devices)&&ready(*r));
        CHECK(r->snapshot()->leaseState==L::Lost&&bytes(p)==before);}
    {const auto f=root/"corrupt.json";put(f,"corrupt");auto r=make(f);CHECK(r->start(15,devices)&&ready(*r));CHECK(r->snapshot()->leaseState==L::Lost&&bytes(f)=="corrupt");}
    std::filesystem::remove_all(root);return finishTests("energy runtime");
}
