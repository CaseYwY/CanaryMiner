// Freeze only this owner's thread before its first native call. The self helper never opens the supplied store path.
#include "product/credit_owner.h"
#include "product/native_process_admission.h"
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
#define OM_TEST_CREDIT_QUEUED_NATIVE 1
#endif
#endif
#if defined(OM_TEST_CREDIT_QUEUED_NATIVE)
namespace {
std::atomic<bool> armed{true},releaseThread{false};std::atomic<unsigned> spawns{0};
std::atomic<bool> raiseAtCreation{false},hookAdmitted{false};om::NativeProcessAdmission creationFault;
void *(*entry)(void *)=nullptr;void *argument=nullptr;
void *gated(void *){while(!releaseThread.load())sched_yield();return entry(argument);}
}
extern "C" int pthread_create(pthread_t *thread,const pthread_attr_t *attributes,void *(*function)(void *),void *arg)noexcept{
    using Create=int (*)(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
    static const auto real=reinterpret_cast<Create>(dlsym(RTLD_NEXT,"pthread_create"));if(!real)return EAGAIN;
    if(armed.exchange(false)){entry=function;argument=arg;return real(thread,attributes,gated,nullptr);}
    return real(thread,attributes,function,arg);
}
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *attributes,char *const argv[],char *const env[]){
    const auto result=__real_pidfd_spawn(fd,path,actions,attributes,argv,env);
    if(!result){++spawns;if(raiseAtCreation.exchange(false)){
        hookAdmitted=creationFault.begin();if(hookAdmitted.load()){creationFault.markUnresolved();creationFault.settled();}}}
    return result;
}
int main(int argc,char **argv){
    if(argc>=2&&std::string(argv[1])=="--local-task-v1"){
        std::string request;if(!om::readLocalTaskInput(argc,argv,&request))return 2;
        if(request=="SYNTHETIC\n")return om::writeLocalTaskOutput("SYNTHETIC INVALID CREDIT REPLY\n")?0:3;
        om::CreditProcessRequest parsed;if(!om::decodeCreditRequest(request,&parsed)||!om::sameCreditSourceContract(parsed.binding,credit_fixture::contract()))return 2;
        // Strict synthetic unknown/empty totals, not a store result or actual income. Never execute/open the supplied path.
        om::CreditProcessReply reply;reply.generation=parsed.generation;reply.ticket=parsed.ticket;reply.opened=om::CreditLedgerStatus::Ok;
        if(parsed.recovery!=om::CreditRecoveryMode::Volatile){om::CreditRecoverySnapshot recovery;recovery.status=om::CreditLedgerStatus::Ok;recovery.state=om::CreditRecoveryState::Empty;
            if(parsed.batch){recovery.state=om::CreditRecoveryState::Confirmed;recovery.batch=parsed.batch;recovery.applied=om::CreditLedgerStatus::Ok;}reply.recovery=std::move(recovery);}
        if(parsed.batch)reply.applied=om::CreditLedgerStatus::Ok;
        for(const auto &q:parsed.queries){om::CreditTotals total;total.status=om::CreditLedgerStatus::Ok;total.qualification=parsed.binding.qualification;total.scope=parsed.binding.scope;
            total.fromExclusiveMs=q.cumulative?parsed.binding.historyStartMs-1:q.fromExclusiveMs;total.throughInclusiveMs=q.throughInclusiveMs;
            total.requestedMs=static_cast<uint64_t>(total.throughInclusiveMs-total.fromExclusiveMs);reply.totals.push_back(std::move(total));}
        std::string wire;if(!om::encodeCreditReply(parsed,reply,&wire))return 2;
        return om::writeLocalTaskOutput(wire)?0:3;
    }
    const std::string mode=argc==1?"queued":argc==2?argv[1]:"";
    if(mode!="queued"&&mode!="ready"&&mode!="ordinary"&&mode!="unknown")return 4; // never recurse into parent ownership for helper/unknown arguments
    armed=mode=="queued";
    const auto exe=std::filesystem::absolute(argv[0]);const auto path=exe.parent_path()/"synthetic-credit-never-opened.sqlite";
    CHECK(!std::filesystem::exists(path));om::CreditLedgerOwner owner(exe,path,credit_fixture::contract(),{},[]{return credit_fixture::T;});
    const auto complete=[&](uint64_t ticket=1){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);auto view=owner.snapshot();
        while(view->state!=om::CreditOwnerState::Faulted&&!(view->state==om::CreditOwnerState::Ready&&!view->pendingJobs&&view->lastCompletedTicket>=ticket)&&std::chrono::steady_clock::now()<end){std::this_thread::sleep_for(std::chrono::milliseconds(5));view=owner.snapshot();}return view;};
    CHECK(owner.start(1));
    if(mode=="queued"){
        CHECK(!armed.load()&&spawns.load()==0);
        om::NativeProcessAdmission foreign;CHECK(foreign.begin());foreign.markUnresolved();foreign.settled();releaseThread=true;
        const auto view=complete();owner.stop();CHECK(spawns.load()==0);CHECK(view->state==om::CreditOwnerState::Faulted&&view->transport==om::LocalTaskStatus::Unreaped);
    }else{
        const auto initial=complete();CHECK(initial->state==om::CreditOwnerState::Ready&&initial->reply&&spawns.load()==1);
        if(mode=="ordinary"){
            owner.stop();CHECK(owner.start(2));const auto next=complete();CHECK(next->state==om::CreditOwnerState::Ready&&next->generation==2&&spawns.load()==2);owner.stop();
        }else if(mode=="ready"){
            om::NativeProcessAdmission foreign;CHECK(foreign.begin());foreign.markUnresolved();foreign.settled();
            const auto revoked=owner.snapshot();CHECK(revoked->state==om::CreditOwnerState::Faulted&&revoked->transport==om::LocalTaskStatus::Unreaped);
            CHECK(revoked->reply&&initial->reply&&revoked->reply->generation==initial->reply->generation&&revoked->reply->ticket==initial->reply->ticket);
            CHECK(owner.refresh(1).status!=om::CreditEnqueueStatus::Queued);CHECK(owner.submit(1,credit_fixture::batch(1,credit_fixture::T)).status!=om::CreditEnqueueStatus::Queued);
            owner.stop();CHECK(!owner.start(2));CHECK(spawns.load()==1);
        }else{
            auto batch=credit_fixture::batch(1,credit_fixture::T,{credit_fixture::record("synthetic-retained-original",1,credit_fixture::T,"1")});
            raiseAtCreation=true;const auto admitted=owner.submit(1,batch);CHECK(admitted.status==om::CreditEnqueueStatus::Queued);
            const auto view=complete(admitted.ticket);owner.stop();const auto final=owner.snapshot();
            CHECK(hookAdmitted.load()&&spawns.load()==2);CHECK(view->state==om::CreditOwnerState::Faulted&&view->transport==om::LocalTaskStatus::Unreaped);
            CHECK(final->writeOutcomeUnknown&&final->uncertainBatch&&final->uncertainBatch->snapshotId==batch.snapshotId&&final->uncertainBatch->records.size()==1&&final->uncertainBatch->records[0].id==batch.records[0].id);
            CHECK(!owner.start(2));
        }
    }
    CHECK(!std::filesystem::exists(path)&&!om::nativeProcessUnresolved());
    return finishTests("queued credit original native epoch (no store access)");
}
#else
int main(){std::puts("SKIP: queued credit native fixture requires glibc >= 2.40");return 77;}
#endif
