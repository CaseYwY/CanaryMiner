// Original six-owner queues/current views; fixed CPU helpers never execute storage, discovery, GPU or network adapters.
#include "product/native_process_admission.h"
#include "product/billing_compute_owner.h"
#include "product/forecast_owner.h"
#include "product/game_detection_owner.h"
#include "product/market_owner.h"
#include "product/public_json_process.h"
#include "product/safety_telemetry_owner.h"
#include "billing_source_fixture.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <cerrno>
#include <chrono>
#include <thread>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_DATA_NATIVE_LIFETIME 1
#endif
#endif
#if defined(OM_TEST_DATA_NATIVE_LIFETIME)
namespace {
using namespace om;
constexpr auto T=credit_fixture::T;
std::atomic<bool> armed{true},releaseThread{false},hookAdmitted{false};std::atomic<unsigned> spawns{0},fetches{0};
std::atomic<bool> failAllocations{false};std::atomic<unsigned> rejectedAllocations{0};
void *(*entry)(void *)=nullptr;void *argument=nullptr;
void *gated(void *){while(!releaseThread.load())sched_yield();return entry(argument);}
template<class P>bool until(P p){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(!p()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return p();}
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
int helper(int argc,char **argv){
    std::string input,wire;if(!readLocalTaskInput(argc,argv,&input))return 2;
    if(input=="SYNTHETIC\n")return writeLocalTaskOutput("SYNTHETIC DATA FIXTURE ONLY\n")?0:3;
    uint64_t parent=0;GameProbeRequest game;
    if(decodeGameProbeRequest(input,&game,&parent)){
        if(!bindLocalTaskParent(parent))return 2;
        GameProbeReply reply;reply.status=GameProbeStatus::Ok;
        reply.privateScope=game.privateScope.empty()?std::string(32,'a'):game.privateScope;reply.began=reply.finished=gameDetectionTime();
        if(!encodeGameProbeReply(game,reply,&wire))return 2;
        return writeLocalTaskOutput(wire)?0:3;
    }
    SafetyTelemetryRequest safety;
    if(decodeSafetyTelemetryRequest(input,&safety,&parent)){
        if(!bindLocalTaskParent(parent))return 2;
        SafetyTelemetryReply reply;reply.status=SafetyTelemetryStatus::Ok;reply.began=reply.finished=safetyTelemetryTime();
        for(size_t i=0;i<safety.devices.size();++i)reply.readings.push_back({40,0,511});
        if(!encodeSafetyTelemetryReply(safety,reply,&wire))return 2;
        return writeLocalTaskOutput(wire)?0:3;
    }
    return writeLocalTaskOutput("SYNTHETIC INVALID READONLY REPLY\n")?0:3;
}
std::shared_ptr<BillingOwnerBinding> context(const std::filesystem::path &file){auto value=std::make_shared<BillingOwnerBinding>();value->file=file;
    value->binding=billing_source_fixture::binding(file.parent_path()/"synthetic-never-source.db");return value;}
std::shared_ptr<const BillingOwnerSnapshot> monetary(const std::shared_ptr<const BillingOwnerBinding> &c){
    auto result=std::make_shared<BillingOwnerSnapshot>();result->generation=7;result->revision=1;result->context=c;result->state=BillingOwnerState::Ready;result->reviewable=true;
    auto view=std::make_shared<BillingOwnerView>();view->stable=true;auto stage=std::make_shared<BillingInputSnapshot>();stage->status=BillingStatus::Ok;view->stage=stage;
    auto money=std::make_shared<BillingRecoverySnapshot>();money->status=BillingStatus::Ok;money->state=BillingRecoveryState::Empty;money->committed.status=BillingStatus::Ok;
    money->committed.opening.throughUtcMs=c->binding.cycleBeginUtcMs;
    money->committed.source=BillingSnapshot::SourceProgress{{0,sha256Hex(energy_history_detail::bindingText(c->binding.source->binding))},billing_source_fixture::now};
    view->monetary=money;result->view=view;return result;
}
}
bool canaryNativeAllocationFault(){if(!failAllocations.load())return false;++rejectedAllocations;return true;}
extern "C" int pthread_create(pthread_t *thread,const pthread_attr_t *attributes,void *(*function)(void *),void *arg)noexcept{
    using Create=int (*)(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
    static const auto real=reinterpret_cast<Create>(dlsym(RTLD_NEXT,"pthread_create"));if(!real)return EAGAIN;
    if(armed.exchange(false)){entry=function;argument=arg;return real(thread,attributes,gated,nullptr);}return real(thread,attributes,function,arg);
}
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *attributes,char *const argv[],char *const env[]){
    const auto result=__real_pidfd_spawn(fd,path,actions,attributes,argv,env);if(!result)++spawns;return result;
}
int main(int argc,char **argv){
    if(argc>=2&&std::string(argv[1])=="--local-task-v1")return helper(argc,argv);
    if(argc==8){PublicJsonResponse reply;reply.status=PublicHttpsStatus::Ok;reply.httpStatus=200;reply.observedUtc=T/1000;
        reply.body="[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.50\"}]";std::string wire;if(!encodePublicJsonResponse(reply,&wire))return 2;
        return std::fwrite(wire.data(),1,wire.size(),stdout)==wire.size()?0:3;}
    if(argc!=3)return 4;
    const std::string kind=argv[1],action=argv[2];
    const bool allocation=action=="ready-allocation",stopped=action=="ready-stopped",foreign=action=="ready-foreign";
    const bool cached=action=="ready"||allocation||stopped||foreign;
    if((kind!="billing"&&kind!="compute"&&kind!="forecast"&&kind!="market"&&kind!="game"&&kind!="safety")||
        (action!="queued"&&action!="ordinary"&&!cached&&action!="callback")||
        ((cached||action=="callback")&&(kind!="market"&&kind!="game"&&kind!="safety"))||(action=="callback"&&kind!="market"))return 4;
    const bool queued=action=="queued",callback=action=="callback";armed=!cached;
    const auto oldEpoch=nativeProcessEpoch();if(foreign)fault();
    const auto exe=std::filesystem::absolute(argv[0]),file=exe.parent_path()/"synthetic-data-never-opened.db";CHECK(!std::filesystem::exists(file));
    const auto release=[&]{CHECK(!armed&&spawns==0&&fetches==0);if(queued)fault();releaseThread=true;};
    if(kind=="billing"){
        const auto c=context(file);BillingOwner owner({exe,exe,exe,exe},*c,{true,1000},[]{return BillingOwnerTime{billing_source_fixture::now,10000};});CHECK(owner.start(7));release();
        CHECK(until([&]{const auto s=owner.snapshot();return s->state==BillingOwnerState::Faulted||s->completedTicket==1;}));const auto s=owner.snapshot();
        CHECK(queued?spawns==0&&s->state==BillingOwnerState::Faulted&&s->transport==LocalTaskStatus::Unreaped:spawns>0&&s->completedTicket==1);owner.stop();
    }else if(kind=="compute"){
        const auto c=context(file);BillingComputeOwner owner(exe,c,{true,1000,512},[]{return BillingOwnerTime{billing_source_fixture::now,10000};});CHECK(owner.start(7));CHECK(owner.observe(7,monetary(c)));
        BillingSourceNotice notice{{1,std::string(64,'f')},c->binding.cycleBeginUtcMs+1000,billing_source_fixture::now};CHECK(owner.notify(7,owner.snapshot()->revision,notice)==BillingComputeAdmission::Queued);release();
        CHECK(until([&]{const auto s=owner.snapshot();return s->state==BillingComputeState::Faulted||s->completedTicket==1;}));const auto s=owner.snapshot();
        CHECK(queued?spawns==0&&s->state==BillingComputeState::Faulted&&s->transport==LocalTaskStatus::Unreaped:spawns>0&&s->completedTicket==1);owner.stop();
    }else if(kind=="forecast"){
        auto binding=credit_fixture::contract();binding.cursor=CreditCursorContract{"fixture.exclusive-acyclic-v1","fixture:initial"};ForecastOwnerPolicy policy;policy.enabled=true;
        IncomeForecastOwner owner(exe,file,binding,policy,[]{return ForecastTime{T,10000};});CHECK(owner.start(7));ForecastScenario scenario;scenario.reportUtcMs=T;scenario.remaining.work.binding=binding;
        CHECK(owner.submit(7,scenario)==ForecastAdmission::Queued);release();CHECK(until([&]{const auto s=owner.snapshot();return s->state==ForecastOwnerState::Faulted||s->completedTicket==1;}));const auto s=owner.snapshot();
        CHECK(queued?spawns==0&&s->state==ForecastOwnerState::Faulted&&s->transport==LocalTaskStatus::Unreaped:spawns>0&&s->completedTicket==1);owner.stop();
    }else if(kind=="game"){
        GameDetectionPolicy policy;policy.enabled=true;GameDetectionOwner owner(exe,{"fixture.game","fixture-owned-game",GameProcessAdapter::LinuxUidNativeImage},policy);CHECK(owner.start(7));if(!cached)release();
        CHECK(until([&]{const auto s=owner.snapshot();return s->state==GameDetectionOwnerState::Faulted||s->completed>0;}));auto s=owner.snapshot();
        if(cached){CHECK(s->state==GameDetectionOwnerState::Ready&&s->detection.state==GamePresence::Absent&&spawns>0);const auto old=s;
            if(foreign){{NativeProcessScope scope(oldEpoch);CHECK(owner.snapshot()->detection.state==GamePresence::Unknown);}CHECK(owner.snapshot()->detection.state==GamePresence::Absent);}
            if(stopped)owner.requestStop();
            failAllocations=allocation;fault();s=owner.snapshot();failAllocations=false;
            CHECK(allocation?s->detection.state==GamePresence::Unknown:s->state==(stopped?GameDetectionOwnerState::Stopped:GameDetectionOwnerState::Faulted)&&s->detection.state==GamePresence::Unknown&&s->transport==LocalTaskStatus::Unreaped);CHECK(old->detection.state==GamePresence::Absent);}
        else CHECK(queued?spawns==0&&s->state==GameDetectionOwnerState::Faulted&&s->transport==LocalTaskStatus::Unreaped:spawns>0&&s->state==GameDetectionOwnerState::Ready);
        owner.stop();
    }else if(kind=="safety"){
        SafetyTelemetryPolicy policy;policy.enabled=true;SafetyTelemetryOwner owner(exe,{"GPU-aaaaaaaa-0000-0000-0000-000000000001"},policy);CHECK(owner.start(7));if(!cached)release();
        CHECK(until([&]{const auto s=owner.snapshot();return s->state==SafetyTelemetryOwnerState::Faulted||s->completed>0;}));auto s=owner.snapshot();
        if(cached){CHECK(s->state==SafetyTelemetryOwnerState::Ready&&s->reply&&spawns>0);const auto old=s;
            if(foreign){{NativeProcessScope scope(oldEpoch);CHECK(!owner.snapshot()->reply);}CHECK(owner.snapshot()->reply&&owner.snapshot()->status==SafetyTelemetryStatus::Ok);}
            if(stopped)owner.requestStop();
            failAllocations=allocation;fault();s=owner.snapshot();failAllocations=false;
            CHECK(allocation?!s->reply:s->state==(stopped?SafetyTelemetryOwnerState::Stopped:SafetyTelemetryOwnerState::Faulted)&&!s->reply&&s->transport==LocalTaskStatus::Unreaped);CHECK(old->reply&&old->reply->status==SafetyTelemetryStatus::Ok);}
        else CHECK(queued?spawns==0&&s->state==SafetyTelemetryOwnerState::Faulted&&s->transport==LocalTaskStatus::Unreaped:spawns>0&&s->state==SafetyTelemetryOwnerState::Ready);
        owner.stop();
    }else{
        auto native=marketProcessFetch(exe);MarketDataOwner owner([&](const PublicJsonRequest &request,const std::atomic<bool> &cancel,PublicHttpsLimits limits){++fetches;
                if(callback){PublicJsonResponse reply;reply.status=PublicHttpsStatus::Ok;reply.httpStatus=200;reply.observedUtc=T/1000;reply.body="[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.50\"}]";
                    NativeProcessAdmission other;hookAdmitted=other.begin();if(hookAdmitted){other.markUnresolved();other.settled();}return reply;}return native(request,cancel,limits);},
            {},MarketOrigin::SyntheticFixture,false,[]{return std::pair<uint64_t,uint64_t>{10000,static_cast<uint64_t>(T/1000)};});CHECK(owner.start(7));if(!cached)release();
        CHECK(until([&]{const auto s=owner.snapshot();return s->helperFault||s->pearlQuote.has_value();}));const auto old=owner.snapshot();
        if(cached){CHECK(old->running&&old->pearlQuote&&spawns>0);
            if(foreign){{NativeProcessScope scope(oldEpoch);CHECK(!owner.snapshot()->running);}CHECK(owner.snapshot()->running);}
            if(stopped)owner.requestStop();
            failAllocations=allocation;fault();}
        const auto s=owner.snapshot();failAllocations=false;
        CHECK((queued||cached||callback)?!s->running&&s->helperFault:spawns>0&&s->pearlQuote.has_value());
        if(queued)CHECK(spawns==0&&fetches==0);
        if(callback)CHECK(hookAdmitted&&spawns==0&&!s->pearlQuote);
        owner.stop();
    }
    CHECK(!allocation||rejectedAllocations>0);CHECK(!std::filesystem::exists(file)&&!std::filesystem::exists(exe.parent_path()/"synthetic-never-source.db")&&!nativeProcessUnresolved());
    return finishTests("six data/safety owner original native queues and cached authority (CPU synthetic helpers only)");
}
#else
bool canaryNativeAllocationFault(){return false;}
int main(){std::puts("SKIP: data owner native lifetime fixture requires glibc >= 2.40");return 77;}
#endif
