// Actual parent/thread/native boundaries; strict self helpers return synthetic empty facts and never open a store.
#include "product/native_process_admission.h"
#include "product/energy_authorization_owner.h"
#include "product/energy_collector_process.h"
#include "product/energy_history_model.h"
#include "product/energy_leased_capture.h"
#include "product/energy_runtime.h"
#include "product/hash.h"
#include "energy_history_fixture.h"
#include "product_test_util.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <thread>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_ENERGY_NATIVE_LIFETIME 1
#endif
#endif
#if defined(OM_TEST_ENERGY_NATIVE_LIFETIME)
namespace {
using namespace om;
constexpr auto T=energy_history_fixture::T;
std::atomic<bool> armed{false},releaseThread{false},raiseAtCreation{false},refuseCreation{false},creationAdmitted{false};
std::atomic<unsigned> spawns{0},attempts{0},refuseOrdinal{0},faultOrdinal{0};NativeProcessAdmission creationFault;
thread_local bool failAllocations=false;std::atomic<unsigned> rejectedAllocations{0};
void *(*entry)(void *)=nullptr;void *argument=nullptr;
void *gated(void *){while(!releaseThread.load())sched_yield();return entry(argument);}
const std::vector<std::string> devices={"GPU-aaaaaaaa-0000-0000-0000-000000000001"};
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
template<class P>bool until(P p){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(!p()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return p();}
EnergyAuthorizationDocument authorized(){EnergyAuthorizationDocument d;d.revision=3;d.changedUtcMs=T;
    d.policy.persistenceAuthorized=true;d.enrollment=EnergyEnrollment::Ready;d.privateNamespace=std::string(32,'1');
    if(!makeEnergyCaptureBinding(d.privateNamespace,T,devices,PowerOrigin::ObservedTelemetry,&d.binding))std::abort();
    return d;}
int helper(int argc,char **argv){
    std::string input,wire;if(!readLocalTaskInput(argc,argv,&input))return 2;
    if(input=="SYNTHETIC\n")return writeLocalTaskOutput("SYNTHETIC ENERGY FIXTURE ONLY\n")?0:3;
    EnergyHistoryRequest history;EnergyCollectorGuardRequest guard;
    if(decodeEnergyHistoryRequest(input,&history)||decodeEnergyCollectorGuardRequest(input,&guard)){
        if(history.generation==0)history=guard.history;
        EnergyHistoryReply reply;reply.generation=history.generation;reply.ticket=history.ticket;reply.opened=EnergyHistoryStatus::Ok;
        auto &head=reply.checkpoint;head.status=EnergyHistoryStatus::Ok;head.digest=sha256Hex(energy_history_detail::bindingText(history.binding));
        EnergyRecoverySnapshot recovery;recovery.status=EnergyHistoryStatus::Ok;recovery.state=EnergyRecoveryState::Empty;recovery.priorDigest=head.digest;
        if(history.batch){const auto &b=*history.batch;reply.applied=EnergyHistoryStatus::Ok;head.sequence=b.sequence;head.batchId=b.id;head.sessionId=b.sessionId;
            head.readingCount=b.readings.size();head.windowCount=b.windows.size();head.sessionCount=b.start?1:0;head.observedAtMs=b.observedAtMs;
            for(const auto &r:b.readings)head.throughUtcMs=(std::max)(head.throughUtcMs,r.utcMs);
            head.digest=sha256Hex(head.digest+"|"+energy_history_detail::batchText(b));recovery.state=EnergyRecoveryState::Confirmed;recovery.batch=b;recovery.applied=reply.applied;}
        if(history.recovery!=EnergyRecoveryMode::Volatile)reply.recovery=recovery;
        if(!encodeEnergyHistoryReply(history,reply,&wire))return 2;
        return writeLocalTaskOutput(wire)?0:3;
    }
    EnergyAuthorizationRequest request;EnergyCollectorLeaseRequest lease;
    const bool held=decodeEnergyCollectorLeaseRequest(input,&lease);
    if(held)request=lease.provision;else if(!decodeEnergyAuthorizationRequest(input,&request))return 2;
    EnergyAuthorizationReply reply;reply.generation=request.generation;reply.ticket=request.ticket;
    if(held||request.file.filename()=="synthetic-energy-held.json"){
        auto d=authorized();if(held){++d.revision;d.changedUtcMs=request.nowUtcMs;d.lastSessionId="session."+std::string(32,'3');reply.sessionIssued=true;}
        reply.status=EnergyAuthorizationStatus::Ok;reply.document=d;
        if(!encodeEnergyAuthorizationDocument(d,&wire))return 2;
        reply.token=sha256Hex(wire);
    }else if(request.action==EnergyAuthorizationAction::Load){reply.status=EnergyAuthorizationStatus::Missing;reply.token="absent";}
    else{EnergyAuthorizationDocument d;d.revision=1;d.changedUtcMs=request.nowUtcMs;d.policy=request.policy;d.limits=request.limits;
        reply.status=EnergyAuthorizationStatus::Ok;reply.document=d;if(!encodeEnergyAuthorizationDocument(d,&wire))return 2;reply.token=sha256Hex(wire);}
    if(held){if(!bindLocalTaskParent(lease.parentPid)||!encodeEnergyCollectorLeaseReply(lease,{reply,std::string(32,'c')},&wire)||!writeLocalTaskOutput(wire+"\n"))return 3;
        std::this_thread::sleep_for(std::chrono::seconds(15));return 0;}
    if(!encodeEnergyAuthorizationReply(request,reply,&wire))return 2;
    return writeLocalTaskOutput(wire)?0:3;
}
}
bool canaryNativeAllocationFault(){if(!failAllocations)return false;++rejectedAllocations;return true;}
extern "C" int pthread_create(pthread_t *thread,const pthread_attr_t *attributes,void *(*function)(void *),void *arg)noexcept{
    using Create=int (*)(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
    static const auto real=reinterpret_cast<Create>(dlsym(RTLD_NEXT,"pthread_create"));if(!real)return EAGAIN;
    if(armed.exchange(false)){entry=function;argument=arg;return real(thread,attributes,gated,nullptr);}return real(thread,attributes,function,arg);
}
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *attributes,char *const argv[],char *const env[]){
    const auto number=++attempts;
    if(refuseCreation.exchange(false)||refuseOrdinal==number){creationAdmitted=creationFault.begin();if(creationAdmitted){creationFault.markUnresolved();creationFault.settled();}return ENOSYS;}
    const auto result=__real_pidfd_spawn(fd,path,actions,attributes,argv,env);if(!result){++spawns;if(raiseAtCreation.exchange(false)||faultOrdinal==number){
        creationAdmitted=creationFault.begin();if(creationAdmitted){creationFault.markUnresolved();creationFault.settled();}}}return result;
}
int main(int argc,char **argv){
    if(argc>=2&&std::string(argv[1])=="--local-task-v1")return helper(argc,argv);
    if(argc!=2)return 4;
    std::string mode=argv[1];const std::string suffix="-allocation";
    const bool allocation=mode.size()>suffix.size()&&mode.compare(mode.size()-suffix.size(),suffix.size(),suffix)==0;
    if(allocation)mode.resize(mode.size()-suffix.size());
    const std::vector<std::string> modes={"history-queued","history-ready","history-ordinary","history-unknown","history-create-unknown","history-create-unissued","history-scope",
        "authorization-queued","authorization-ready","authorization-unknown","authorization-unissued","authorization-scope","leased-queued","leased-held","leased-unissued","leased-unknown","leased-scope","capture-ready","runtime-queued","runtime-ready","runtime-stopped","runtime-scope"};
    if(std::find(modes.begin(),modes.end(),mode)==modes.end())return 4; // unknown helper modes cannot recurse into a parent owner
    if(allocation&&mode!="history-ready"&&mode!="authorization-ready"&&mode!="leased-held"&&mode!="capture-ready"&&mode!="runtime-ready")return 4;
    const bool queued=mode.find("queued")!=std::string::npos,scoped=mode.find("scope")!=std::string::npos;
    armed=queued||scoped;const auto original=nativeProcessEpoch();
    const bool unissued=mode.find("unissued")!=std::string::npos;
    const auto exe=std::filesystem::absolute(argv[0]);const auto file=exe.parent_path()/((mode=="leased-held"||mode=="leased-unissued"||mode=="leased-unknown")?"synthetic-energy-held.json":"synthetic-energy-never-opened.db");
    CHECK(!std::filesystem::exists(file));
    const auto begin=[&](const auto &start){if(scoped){fault();NativeProcessScope scope(original);CHECK(!start());CHECK(armed&&spawns==0);releaseThread=true;return false;}
        CHECK(start());if(queued){CHECK(!armed&&spawns==0);fault();releaseThread=true;}return true;};
    if(mode.rfind("history-",0)==0){
        EnergyHistoryOwner owner(exe,file,energy_history_fixture::binding(),{},1000,[]{return T+20000;});
        const bool creation=mode=="history-create-unknown"||mode=="history-create-unissued";raiseAtCreation=creation&&!unissued;refuseCreation=creation&&unissued;
        if(begin([&]{return owner.start(1,creation?EnergyHistoryOpen::CreateNew:EnergyHistoryOpen::Existing);})){CHECK(until([&]{const auto s=owner.snapshot();return s->state==EnergyHistoryOwnerState::Ready||s->state==EnergyHistoryOwnerState::Faulted;}));
            if(queued){const auto s=owner.snapshot();CHECK(spawns==0&&s->state==EnergyHistoryOwnerState::Faulted&&s->transport==LocalTaskStatus::Unreaped);}
            else if(creation){owner.stop();CHECK(creationAdmitted&&owner.snapshot()->creationOutcomeUnknown==!unissued&&spawns==(unissued?0u:1u));CHECK(!owner.start(2));}
            else{auto ready=owner.snapshot();CHECK(ready->state==EnergyHistoryOwnerState::Ready&&ready->reply&&spawns==1);
                if(mode=="history-ordinary"){owner.stop();CHECK(owner.start(2));CHECK(until([&]{return owner.snapshot()->state==EnergyHistoryOwnerState::Ready;}));}
                else if(mode=="history-ready"){failAllocations=allocation;fault();auto revoked=owner.snapshot();
                    CHECK(allocation?revoked->state!=EnergyHistoryOwnerState::Ready:revoked->state==EnergyHistoryOwnerState::Faulted&&revoked->transport==LocalTaskStatus::Unreaped);
                    CHECK(allocation|| (ready->reply&&revoked->reply&&ready->reply->ticket==revoked->reply->ticket));CHECK(owner.refresh(1,revoked->revision)!=EnergyHistoryAdmission::Queued);failAllocations=false;
                    owner.stop();CHECK(!owner.start(2));}
                else{auto batch=energy_history_fixture::first();raiseAtCreation=true;CHECK(owner.submit(1,ready->revision,batch)==EnergyHistoryAdmission::Queued);
                    CHECK(until([&]{return owner.snapshot()->state==EnergyHistoryOwnerState::Faulted;}));owner.stop();auto s=owner.snapshot();
                    CHECK(creationAdmitted&&s->uncertainBatch&&s->uncertainBatch->id==batch.id&&s->uncertainBatch->readings.size()==batch.readings.size());CHECK(!owner.start(2));}}
        }owner.stop();
    }else if(mode.rfind("authorization-",0)==0){
        EnergyAuthorizationOwner owner(exe,file,false,1000,[]{return T+20000;});
        if(begin([&]{return owner.start(1);})){CHECK(until([&]{const auto s=owner.snapshot();return s->state==EnergyAuthorizationOwnerState::Ready||s->state==EnergyAuthorizationOwnerState::Faulted;}));
            if(queued){const auto s=owner.snapshot();CHECK(spawns==0&&s->state==EnergyAuthorizationOwnerState::Faulted&&s->transport==LocalTaskStatus::Unreaped);}
            else{const auto ready=owner.snapshot();CHECK(ready->state==EnergyAuthorizationOwnerState::Ready&&ready->reply&&spawns==1);
                if(mode=="authorization-ready"){failAllocations=allocation;fault();const auto s=owner.snapshot();
                    CHECK(allocation?s->state!=EnergyAuthorizationOwnerState::Ready:s->state==EnergyAuthorizationOwnerState::Faulted&&s->transport==LocalTaskStatus::Unreaped&&!s->planAvailable);
                    CHECK(owner.submit(1,s->revision,{})!=EnergyHistoryAdmission::Queued);CHECK(allocation||(ready->reply&&s->reply&&ready->reply->ticket==s->reply->ticket));failAllocations=false;}
                else{EnergyAuthorizationCommand command;command.action=EnergyAuthorizationAction::Save;raiseAtCreation=!unissued;refuseCreation=unissued;
                    CHECK(owner.submit(1,ready->revision,command)==EnergyHistoryAdmission::Queued);CHECK(until([&]{return owner.snapshot()->state==EnergyAuthorizationOwnerState::Faulted;}));
                    owner.stop();const auto s=owner.snapshot();CHECK(creationAdmitted&&(unissued?!s->uncertain:s->uncertain&&s->uncertain->action==command.action&&s->uncertain->ticket==2));}}
        }owner.stop();
    }else if(mode.rfind("leased-",0)==0){
        auto owner=makeLeasedEnergyCapture(exe,exe,file,false,1000,[]{return T+20000;});
        if(unissued)refuseOrdinal=2;else if(mode=="leased-unknown")faultOrdinal=2;
        if(begin([&]{return owner->start(1,devices);})){if(queued){CHECK(until([&]{return owner->snapshot()->leaseState!=EnergyLeaseState::Starting;}));const auto s=owner->snapshot();
                CHECK(spawns==0&&s->state==EnergyCaptureState::Faulted&&s->leaseState==EnergyLeaseState::Lost&&s->leaseTransport==LocalTaskStatus::Unreaped);}
            else if(unissued||mode=="leased-unknown"){CHECK(until([&]{return owner->snapshot()->leaseState==EnergyLeaseState::Lost;}));owner->stop();const auto s=owner->snapshot();
                CHECK(creationAdmitted&&spawns==(unissued?1u:2u));CHECK(s->provisioningOutcomeUnknown==!unissued);
                CHECK(unissued?!s->provisionRequest:s->provisionRequest&&s->provisionRequest->action==EnergyAuthorizationAction::Provision&&s->provisionRequest->devices==devices);}
            else{CHECK(until([&]{owner->tick(T+20000,20000);return owner->snapshot()->state==EnergyCaptureState::Collecting;}));const auto before=owner->snapshot();
                CHECK(before->leaseState==EnergyLeaseState::Held&&spawns==3);failAllocations=allocation;fault();const auto s=owner->snapshot();
                CHECK(allocation?s->leaseState!=EnergyLeaseState::Held:s->state==EnergyCaptureState::Faulted&&s->leaseState==EnergyLeaseState::Lost);
                CHECK(owner->submit(1,{0,T+20000,20000,1000})==EnergyCaptureAdmission::Stopped);failAllocations=false;CHECK(before->leaseState==EnergyLeaseState::Held);}}
        owner->stop();
    }else if(mode=="capture-ready"){
        EnergyCapturePlan plan;plan.privateNamespace=std::string(32,'1');plan.sessionId="fixture.original-parent";plan.policy.persistenceAuthorized=true;
        const std::vector<std::string> ids={"fixture.board"};CHECK(makeEnergyCaptureBinding(plan.privateNamespace,T,ids,PowerOrigin::SyntheticFixture,&plan.binding));
        auto owner=makeEnergyCapture(exe,file,plan,1000,[]{return T+20000;});CHECK(owner->start(1,ids));
        CHECK(until([&]{owner->tick(T+20000,20000);return owner->snapshot()->state==EnergyCaptureState::Collecting;}));const auto before=owner->snapshot();failAllocations=allocation;fault();
        const auto s=owner->snapshot();CHECK(allocation?s->state!=EnergyCaptureState::Collecting:s->state==EnergyCaptureState::Faulted&&s->owner&&s->owner->transport==LocalTaskStatus::Unreaped);
        CHECK(owner->submit(1,{0,T+20000,20000,1000})==EnergyCaptureAdmission::Stopped);failAllocations=false;CHECK(before->state==EnergyCaptureState::Collecting);owner->stop();
    }else{
        auto owner=makeEnergyRuntime(exe,exe,file,true,false,1000,[]{return T+20000;});
        if(begin([&]{return owner->start(1,devices);})){if(mode=="runtime-ready"||mode=="runtime-stopped"){
                CHECK(until([&]{owner->tick(T+20000,20000);return owner->authorizationSnapshot()->state==EnergyAuthorizationOwnerState::Ready;}));
                const auto before=owner->authorizationSnapshot();CHECK(before->reply&&spawns==2);if(mode=="runtime-stopped")owner->requestStop();failAllocations=allocation;fault();
                const auto a=owner->authorizationSnapshot();
                CHECK(mode=="runtime-stopped"?a->state==EnergyAuthorizationOwnerState::Stopped&&a->transport==LocalTaskStatus::Unreaped:
                    allocation?a->state!=EnergyAuthorizationOwnerState::Ready:a->state==EnergyAuthorizationOwnerState::Faulted&&a->transport==LocalTaskStatus::Unreaped);
                if(mode=="runtime-stopped"){const auto s=owner->snapshot();CHECK(s->state==EnergyCaptureState::Stopped&&s->leaseState==EnergyLeaseState::Stopped&&s->leaseTransport==LocalTaskStatus::Unreaped);}
                CHECK(owner->authorize(1,a->revision,{})!=EnergyHistoryAdmission::Queued);failAllocations=false;CHECK(before->state==EnergyAuthorizationOwnerState::Ready);
            }else{CHECK(until([&]{owner->tick(T+20000,20000);return !owner->authorizationSnapshot()->startupPending;}));
                CHECK(spawns==0);CHECK(owner->snapshot()->state==EnergyCaptureState::Faulted&&owner->authorizationSnapshot()->state==EnergyAuthorizationOwnerState::Faulted);}}
        owner->stop();
    }
    CHECK(!allocation||rejectedAllocations>0);
    CHECK(!std::filesystem::exists(file)&&!nativeProcessUnresolved());
    return finishTests("original energy owner/capture/runtime native lifetimes (synthetic no-store helpers)");
}
#else
bool canaryNativeAllocationFault(){return false;}
int main(){std::puts("SKIP: energy native lifetime fixture requires glibc >= 2.40");return 77;}
#endif
