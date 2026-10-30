// Real CPU evaluator and protocol issuer/gate. No process, device, network, provider or installed miner.
#include "economic_fixture.h"
#include "product/economic_command_owner.h"
#include "product_test_util.h"
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace economic_fixture;
namespace {
using S=EconomicCommandState;using D=WorkerDirective;using K=WorkerReceiptKind;using R=WorkerReceiptResult;
OptimizationTime at(uint64_t ms=0){return {T+static_cast<int64_t>(ms),1000000+ms};}
WorkerControlBinding binding(){return {std::string(32,'1'),std::string(32,'2'),1,1,0};}
EconomicCommandPolicy policy(uint64_t lease=2000){EconomicCommandPolicy p;p.enabled=p.guard.enabled=p.guard.margins.enabled=p.guard.result.enabled=true;
    p.commandLeaseMs=lease;p.guard.result.maximumResultAgeMs=5000;p.guard.confirmationMs=0;p.guard.minimumEvaluations=1;
    p.guard.maximumConfirmationGapMs=5000;p.guard.cooldownMs=0;p.guard.transitionWindowMs=10000;p.guard.maximumTransitions=2;return p;}
EconomicEvaluationPolicy evaluationPolicy(){EconomicEvaluationPolicy p;p.enabled=p.schedule.enabled=p.result.enabled=true;
    p.schedule.evaluationMs=1000;p.schedule.operationTimeoutMs=500;p.result.maximumResultAgeMs=5000;return p;}
struct Fixture {
    SessionIntent intent;SessionToken token=intent.explicitStart();OptimizationTime now=at();EconomicInput base=input();uint64_t revision=1;unsigned installed=99;
    EconomicCommandPolicy settings;EconomicParentBudget budget;
    EconomicEvaluationOwner evaluation{evaluationPolicy(),intent,token};
    WorkerControlIssuer issuer{binding(),intent,token};WorkerControlGate gate{binding(),[this]{return now.monotonicMs;}};
    std::unique_ptr<EconomicCommandOwner> owner;
    explicit Fixture(uint64_t lease=2000):settings(policy(lease)),budget({true,0,10000,2,4},intent,token){
        base.generation=token.epoch;prices(base);CHECK(evaluation.start(now)&&budget.start(now)&&issuer.start(now.monotonicMs));
        const auto c=issuer.issue(D::Pause,now.monotonicMs,5000);CHECK(c&&gate.receive(*c)==WorkerCommandResult::Accepted);
        gate.pauseBoundary(true);CHECK(issuer.acknowledge(*gate.snapshot().receipt,now.monotonicMs)==R::Settled);
        owner=std::make_unique<EconomicCommandOwner>(settings,binding(),evaluation,issuer,budget,intent,token);CHECK(owner->start(now,base.currentPlan));
    }
    void begin(uint64_t ms,const char *price=nullptr){now=at(ms);if(price){++revision;base.nowUtcMs=now.utcMs;base.nowMonoMs=now.monotonicMs;prices(base,price);}
        const auto ticket=evaluation.poll(now,revision,base.currentPlan,base.conditions).requests[1];CHECK(ticket.has_value());if(!ticket)return;
        auto in=base;in.nowUtcMs=now.utcMs;in.nowMonoMs=now.monotonicMs;in.electricity.opening.throughUtcMs=now.utcMs+1;
        in.electricity.opening.timeDatabaseVersion=civilAt(in.electricity.plan.clock,now.utcMs+1).databaseVersion;
        CHECK(evaluation.submit(*ticket,revision,std::move(in),now)==EconomicSubmission::Accepted);
    }
    void finish(uint64_t ms,bool available=true){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);now=at(ms);
        do{evaluation.poll(now,revision,base.currentPlan,base.conditions);if(!evaluation.snapshot().pending)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<end);
        CHECK(!evaluation.snapshot().pending&&(evaluation.snapshot().study!=nullptr)==available);
    }
    void study(uint64_t ms,const char *price=nullptr){begin(ms,price);finish(ms+1);}
    std::optional<WorkerCommand> poll(uint64_t ms,uint64_t permission=UINT64_MAX){now=at(ms);evaluation.poll(now,revision,base.currentPlan,base.conditions);
        return owner->poll(now,revision,base.currentPlan,base.conditions,permission);}
    WorkerCommand send(uint64_t ms,uint64_t permission=UINT64_MAX){const auto c=poll(ms,permission);CHECK(c.has_value());if(!c)return {};
        CHECK(gate.receive(*c)==WorkerCommandResult::Accepted);return *c;}
    WorkerReceipt applied(uint64_t ms,bool deliver=true){now=at(ms);const auto c=gate.snapshot().receipt->command;
        gate.pauseBoundary(c.directive==D::Pause);if(c.directive==D::Run)CHECK(gate.applyWorkWait(installed)==WorkControlRequest::Run);
        const auto r=*gate.snapshot().receipt;if(deliver)CHECK(owner->acknowledge(r,now)==R::Settled);return r;}
};
void bindingAndRenewal(){Fixture f;f.study(0);const auto c=f.send(1);const auto original=f.owner->snapshot().lastRun;
    CHECK(c.directive==D::Run&&c.hostWaitMs==0&&c.deadlineMonoMs==at(2001).monotonicMs);
    CHECK(original&&sameWorkerCommand(original->command,c)&&original->study==f.evaluation.snapshot().study&&original->decision.contextRevision==1&&
        original->decision.resultSequence==f.evaluation.snapshot().resultSequence&&original->decision.point==0&&original->decision.hostWaitMs==0&&
        original->selected.monotonicMs==at(1).monotonicMs&&original->study->input().profile==f.base.profile);
    CHECK(f.owner->snapshot().pending&&!f.owner->snapshot().application&&f.budget.snapshot().transitionsInWindow==0);
    CHECK(f.owner->acknowledge(*f.gate.snapshot().receipt,at(2))==R::Waiting);CHECK(!f.poll(2)&&f.owner->snapshot().pending);
    const auto r=f.applied(3);CHECK(!f.poll(3)&&f.owner->snapshot().application&&sameWorkerCommand(f.owner->snapshot().application->command,c));
    CHECK(f.budget.snapshot().transitionsInWindow==1&&f.owner->snapshot().application->observedMonoMs==r.observedMonoMs);
    f.study(1000);const auto renewal=f.send(1001);
    CHECK(renewal.hostWaitMs==0&&renewal.deadlineMonoMs>c.deadlineMonoMs&&f.owner->snapshot().lastRun->decision.validUntilMonoMs<=c.deadlineMonoMs&&
        f.owner->snapshot().lastRun->decision.sourceValidUntilMonoMs>c.deadlineMonoMs); // source ceiling, not previous lease, admits renewal
    CHECK(sameWorkerCommand(original->command,c)&&!f.owner->snapshot().application);f.applied(1002);CHECK(!f.poll(1002)&&f.budget.snapshot().transitionsInWindow==1);
    f.study(2000,"0.005");const auto change=f.send(2001);CHECK(change.hostWaitMs==10&&f.owner->snapshot().lastRun->decision.contextRevision==2);
    f.applied(2002);CHECK(!f.poll(2002)&&f.budget.snapshot().transitionsInWindow==2);
    f.study(3000,"0.01");const auto limited=f.send(3001);CHECK(limited.hostWaitMs==10&&f.owner->snapshot().lastRun->decision.reason==EconomicGuardReason::Budget);
    f.applied(3002);CHECK(!f.poll(3002)&&f.budget.snapshot().transitionsInWindow==2);
}
void pendingRevocation(){
    for(unsigned mode=0;mode<10;++mode){Fixture f;f.study(0);const auto c=f.send(1);const auto original=f.owner->snapshot().lastRun;
        if(mode==0)f.base.conditions.safetyTelemetryValid=false;
        if(mode==1)f.base.conditions.thermalSafe=false;
        if(mode==2)f.base.conditions.gameActive=true;
        if(mode==3)f.base.conditions.drift=true;
        if(mode==4)f.base.conditions.fault=true;
        if(mode==7)++f.revision;
        if(mode==8)f.evaluation.requestStop();
        if(mode==9)f.study(1000);
        const uint64_t permission=mode==5?0:mode==6?c.deadlineMonoMs-1:UINT64_MAX;
        CHECK(!f.poll(mode==9?1001:2,permission));const auto s=f.owner->snapshot();
        CHECK_MSG(("pending withdrawal "+std::to_string(mode)).c_str(),s.revokeRequired&&s.pending&&(s.state==S::Faulted||s.state==S::Stopped));
        CHECK(s.lastRun==original&&!s.application&&!f.poll(mode==9?1002:3)&&!f.owner->start(at(4),f.base.currentPlan));
    }
    {Fixture f(5000);f.study(0);const auto c=f.send(1);auto skewed=at(100);skewed.utcMs+=200;
        f.evaluation.poll(skewed,f.revision,f.base.currentPlan,f.base.conditions);
        CHECK(!f.owner->poll(skewed,f.revision,f.base.currentPlan,f.base.conditions,UINT64_MAX));
        CHECK(f.owner->snapshot().state==S::Faulted&&f.owner->snapshot().revokeRequired&&c.deadlineMonoMs==at(5000).monotonicMs);}
    {Fixture f(5000);f.study(0);const auto c=f.send(1);CHECK(!f.poll(4950,c.deadlineMonoMs)&&f.owner->snapshot().state==S::Running&&f.owner->snapshot().pending);
        CHECK(!f.poll(5000,c.deadlineMonoMs)&&f.owner->snapshot().state==S::Faulted&&f.owner->snapshot().revokeRequired);}
}
void pauseAndHistorical(){
    {Fixture f;f.study(0);f.send(1);f.study(1000,"0.005");f.applied(1002);
        const auto pause=f.poll(1002);CHECK_MSG("settled old decision with a pre-application replacement study must pause",pause&&pause->directive==D::Pause);}
    {Fixture f(5000);f.study(0);f.send(1);f.applied(2);CHECK(!f.poll(2));f.study(1000);
        const auto renewed=f.poll(1001);CHECK_MSG("new source proof must bind a new same-wait command before the old lease's renewal point",renewed&&renewed->directive==D::Run&&renewed->hostWaitMs==0&&
            f.owner->snapshot().lastRun->decision.resultSequence==f.evaluation.snapshot().resultSequence);}
    {Fixture f;f.study(0);f.send(1);f.applied(2);f.base.conditions.thermalSafe=false;const auto pause=f.send(3);
        CHECK(pause.directive==D::Pause&&!pause.hostWaitMs&&f.owner->snapshot().state==S::Running&&f.owner->snapshot().application);
        f.applied(4);CHECK(!f.poll(4)&&f.budget.snapshot().transitionsInWindow==1);f.base.conditions.thermalSafe=true;
        CHECK(!f.poll(5));f.study(1000);const auto resume=f.send(1001);CHECK(resume.directive==D::Run&&resume.hostWaitMs==0);
        f.applied(1002);CHECK(!f.poll(1002)&&f.budget.snapshot().transitionsInWindow==2);}
    {Fixture f;f.study(0);const auto c=f.send(1);const auto r=f.applied(2,false);f.base.conditions.gameActive=true;CHECK(!f.poll(3));
        CHECK(f.owner->acknowledge(r,at(4))==R::Terminal&&f.owner->snapshot().application&&!f.owner->snapshot().pending&&f.owner->snapshot().revokeRequired);
        CHECK(sameWorkerCommand(f.owner->snapshot().application->command,c)&&f.owner->snapshot().state==S::Faulted&&!f.owner->retire(at(4)));}
    {Fixture f;f.study(0);f.send(1);f.applied(2);CHECK(!f.poll(2));
        const auto pause=f.send(1901);CHECK(pause.directive==D::Pause);f.applied(1902);CHECK(!f.poll(1902));} // no post-application study: Pause before lease expiry
    {Fixture f;f.study(0);const auto c=f.send(1);const auto old=*f.issuer.snapshot(at(1).monotonicMs).settled;
        CHECK(f.owner->acknowledge(old,at(2))==R::Duplicate&&f.owner->snapshot().pending&&!f.owner->snapshot().application);
        CHECK(sameWorkerCommand(f.owner->snapshot().lastRun->command,c));}
}
void integrityAndRetirement(){
    for(unsigned mode=0;mode<4;++mode){Fixture f;f.study(0);f.send(1);auto r=f.applied(2,false);
        if(mode==0)++r.command.deadlineMonoMs;
        if(mode==1)r.command.binding.workerKey[0]='a';
        if(mode==2)r.command.hostWaitMs=10;
        if(mode==3)++r.command.revision;
        CHECK(f.owner->acknowledge(r,at(3))==R::Rejected&&f.owner->snapshot().state==S::Faulted&&!f.owner->snapshot().application);}
    {Fixture f;f.study(0);const auto c=f.issuer.issue(D::Pause,at(1).monotonicMs,5000);CHECK(c.has_value());
        CHECK(!f.poll(1)&&f.owner->snapshot().state==S::Faulted);}
    {Fixture f;f.study(0);f.send(1);const auto r=f.applied(2,false);CHECK(f.issuer.acknowledge(r,at(2).monotonicMs)==R::Settled);
        CHECK(!f.poll(2)&&f.owner->snapshot().state==S::Faulted&&!f.owner->snapshot().application);}
    {Fixture f;f.study(0);const auto c=f.send(1);const auto r=f.applied(2);f.issuer.requestStop();CHECK(f.owner->retire(at(3)));
        CHECK(f.owner->snapshot().state==S::Retired&&f.budget.snapshot().state==EconomicGuardState::Running&&!f.budget.snapshot().attached&&f.budget.snapshot().transitionsInWindow==1);
        CHECK(!f.owner->retire(at(4))&&sameWorkerCommand(f.owner->snapshot().lastRun->command,c)&&f.owner->snapshot().application->observedMonoMs==r.observedMonoMs);
        auto next=binding();++next.workerGeneration;next.workerKey=std::string(32,'3');WorkerControlIssuer issuer(next,f.intent,f.token);CHECK(issuer.start(at(4).monotonicMs));
        const auto initial=issuer.issue(D::Pause,at(4).monotonicMs,5000);CHECK(initial.has_value());CHECK(issuer.acknowledge({*initial,K::Paused,at(4).monotonicMs},at(4).monotonicMs)==R::Settled);
        EconomicCommandOwner owner(f.settings,next,f.evaluation,issuer,f.budget,f.intent,f.token);CHECK(owner.start(at(4),f.base.currentPlan));
        CHECK(!owner.poll(at(4),1,f.base.currentPlan,f.base.conditions,UINT64_MAX));f.study(1000);
        const auto resume=owner.poll(at(1001),1,f.base.currentPlan,f.base.conditions,UINT64_MAX);CHECK(resume&&resume->directive==D::Run&&resume->hostWaitMs==0);
        owner.requestStop();CHECK(owner.snapshot().revokeRequired);}
    {Fixture f;f.study(0);f.send(1);f.issuer.requestStop();CHECK(!f.owner->retire(at(2))&&f.owner->snapshot().state==S::Faulted);}
    {Fixture f;f.study(0);f.send(1);f.owner->transportFailed();CHECK(!f.poll(2)&&f.owner->snapshot().state==S::Faulted&&f.owner->snapshot().revokeRequired);}
}
void stopAndLimits(){
    for(unsigned mode=0;mode<3;++mode){Fixture f;f.study(0);f.send(1);
        if(mode==0)f.intent.stop();
        if(mode==1)f.owner->requestStop();
        if(mode==2)f.budget.requestStop();
        CHECK(!f.poll(2)&&f.owner->snapshot().state==S::Stopped&&f.owner->snapshot().revokeRequired);
        f.intent.stop();f.intent.explicitStart();CHECK(!f.poll(3)&&!f.owner->start(at(3),f.base.currentPlan));}
    {Fixture f;f.study(0);f.send(2);CHECK(!f.poll(1)&&f.owner->snapshot().state==S::ClockFault&&f.owner->snapshot().revokeRequired);}
    {Fixture f;f.study(0);--f.base.currentPlan.maximumSampleGapMs;CHECK(!f.poll(1)&&f.owner->snapshot().state==S::Faulted);}
    {Fixture f;f.base.candidates[0].permitted=false;f.study(0);const auto c=f.send(1);CHECK(c.hostWaitMs==10&&f.owner->snapshot().lastRun->decision.point==1);}
    {Fixture f;f.study(0);CHECK(!f.poll(1,at(100).monotonicMs)&&f.owner->snapshot().state==S::Running&&!f.owner->snapshot().lastRun);}
}
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
std::mutex buildMutex;std::condition_variable buildWake;bool held=false,entered=false,released=false;
struct BlockBuild {
    BlockBuild(){std::lock_guard<std::mutex> lock(buildMutex);held=true;entered=released=false;}
    bool reached(){std::unique_lock<std::mutex> lock(buildMutex);return buildWake.wait_for(lock,std::chrono::seconds(5),[]{return entered;});}
    void release(){std::lock_guard<std::mutex> lock(buildMutex);released=true;buildWake.notify_all();}
    ~BlockBuild(){std::lock_guard<std::mutex> lock(buildMutex);released=true;held=false;buildWake.notify_all();}
};
void pendingComputation(){
    for(unsigned mode=0;mode<4;++mode){Fixture f;f.study(0);f.send(1);const auto original=f.owner->snapshot().lastRun;
        BlockBuild block;f.begin(1000);CHECK(block.reached());CHECK(!f.poll(1001)&&f.owner->snapshot().state==S::Running&&f.owner->snapshot().pending);
        CHECK(!f.evaluation.snapshot().study&&f.evaluation.snapshot().retainedResultSequence==original->decision.resultSequence);
        uint64_t when=1002;
        if(mode==0){block.release();f.finish(when);}
        if(mode==1)++f.revision;
        if(mode==2)f.base.conditions.thermalSafe=false;
        if(mode==3)when=1500;
        CHECK(!f.poll(when)&&f.owner->snapshot().revokeRequired&&f.owner->snapshot().state==S::Faulted&&f.owner->snapshot().lastRun==original);
        if(mode!=0){block.release();f.finish(when+1,false);}
        CHECK(!f.poll(when+2)&&f.owner->snapshot().state==S::Faulted);
    }
}
#endif
}
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
namespace om {
std::shared_ptr<const EconomicStudy> economicEvaluationTestBuild(const EconomicInput &in,EconomicStatus *status,const std::atomic<bool> *cancel){
    bool ignore=false;
    {std::unique_lock<std::mutex> lock(buildMutex);if(held){ignore=true;entered=true;buildWake.notify_all();
        if(!buildWake.wait_for(lock,std::chrono::seconds(5),[]{return released;}))throw std::runtime_error("synthetic command computation barrier");}}
    return buildEconomicStudy(in,status,ignore?nullptr:cancel);
}
}
#endif
int main(){bindingAndRenewal();pendingRevocation();pauseAndHistorical();integrityAndRetirement();stopAndLimits();
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
    pendingComputation();return finishTests("economic controlled command ownership");
#else
    return finishTests("economic decision-bound commands");
#endif
}
