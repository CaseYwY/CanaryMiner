// Memory-only control owners and real CPU studies. No native helper, device, source, network or worker process.
#include "economic_fixture.h"
#include "product/economic_command_owner.h"
#include "product/work_experiment_owner.h"
#include "product/work_experiment_budget.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
namespace {
using namespace om;using namespace economic_fixture;
std::atomic<bool> rejectAllocations{false};std::atomic<unsigned> allocationAttempts{0};
using D=WorkerDirective;using K=WorkerReceiptKind;using R=WorkerReceiptResult;
OptimizationTime at(uint64_t ms=0){return {T+static_cast<int64_t>(ms),1000000+ms};}
WorkMeasurementTime measureAt(uint64_t ms=0){return {at(ms).utcMs,at(ms).monotonicMs};}
WorkerControlBinding binding(){return {std::string(32,'1'),std::string(32,'2'),1,1,0};}
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
template<class F> bool currentRead(const std::string &action,F read){
    if(action!="allocation")return read();
    bool value=false,threw=false;rejectAllocations=true;try{value=read();}catch(...){threw=true;}rejectAllocations=false;
    CHECK(!threw&&allocationAttempts.load()==0);return value;
}
struct Wire {
    SessionIntent intent;SessionToken token=intent.explicitStart();uint64_t now=at().monotonicMs;
    WorkerControlIssuer issuer{binding(),intent,token};WorkerControlGate gate{binding(),[this]{return now;}};
    WorkProfileConditions conditions{true,true,true,false,false,false};
    Wire(){CHECK(issuer.start(now));const auto c=issuer.issue(D::Pause,now,5000);CHECK(c&&gate.receive(*c)==WorkerCommandResult::Accepted);
        gate.pauseBoundary(true);CHECK(issuer.acknowledge(*gate.snapshot().receipt,now)==R::Settled);}
    void revoke(const std::string &action){if(action=="intent-stop")intent.stop();else fault();}
};
EconomicGuardPolicy guardPolicy(){EconomicGuardPolicy p;p.enabled=p.margins.enabled=p.result.enabled=true;p.result.maximumResultAgeMs=5000;
    p.confirmationMs=p.cooldownMs=0;p.minimumEvaluations=1;p.maximumConfirmationGapMs=5000;p.transitionWindowMs=10000;p.maximumTransitions=2;return p;}
EconomicEvaluationPolicy evaluationPolicy(){EconomicEvaluationPolicy p;p.enabled=p.schedule.enabled=p.result.enabled=true;
    p.schedule.evaluationMs=1000;p.schedule.operationTimeoutMs=500;p.result.maximumResultAgeMs=5000;return p;}
struct Economy {
    Wire wire;EconomicInput in=input();EconomicParentBudget budget{{true,0,10000,2,4},wire.intent,wire.token};
    EconomicEvaluationOwner evaluation{evaluationPolicy(),wire.intent,wire.token};
    Economy(){CHECK(budget.start(at())&&evaluation.start(at()));}
    void study(){const auto request=evaluation.poll(at(),1,in.currentPlan,in.conditions).requests[1];CHECK(request.has_value());if(!request)return;
        in.generation=wire.token.epoch;in.nowUtcMs=request->issued.utcMs;in.nowMonoMs=request->issued.monotonicMs;reward(in);prices(in);
        CHECK(evaluation.submit(*request,1,in,at())==EconomicSubmission::Accepted);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do{evaluation.poll(at(1),1,in.currentPlan,in.conditions);if(!evaluation.snapshot().pending)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<end);
        CHECK(evaluation.snapshot().study&&evaluation.snapshot().use.point.has_value());}
};
void economic(const std::string &kind,const std::string &action,uint64_t foreignEpoch){
    Economy e;
    if(kind=="guard"){
        EconomicChoiceGuard guard(guardPolicy(),binding(),e.evaluation,e.wire.issuer,e.wire.intent,e.wire.token,&e.budget);
        if(action=="foreign-start"){{NativeProcessScope old(foreignEpoch);CHECK(!guard.start(at(),e.in.currentPlan));}
            CHECK(e.wire.issuer.snapshot(at().monotonicMs).state==WorkerControlState::Active&&e.budget.snapshot().state==EconomicGuardState::Running&&!e.budget.snapshot().attached);return;}
        CHECK(guard.start(at(),e.in.currentPlan));e.study();const auto before=guard.check(at(1),1,e.in.currentPlan,e.in.conditions);
        const auto study=e.evaluation.snapshot().study;CHECK(before.state==EconomicGuardState::Running&&before.point&&study);
        if(action=="foreign"){{NativeProcessScope old(foreignEpoch);
            CHECK(guard.snapshot().state!=EconomicGuardState::Running&&!guard.snapshot().point);
            CHECK(e.budget.snapshot().state!=EconomicGuardState::Running&&!e.budget.snapshot().attached);
            CHECK(guard.check(at(900000),1,e.in.currentPlan,e.in.conditions).state!=EconomicGuardState::Running);CHECK(!guard.retire(at(900000)));}
            CHECK(guard.check(at(2),1,e.in.currentPlan,e.in.conditions).state==EconomicGuardState::Running&&guard.snapshot().point==before.point&&e.evaluation.snapshot().study==study);
            CHECK(e.budget.snapshot().state==EconomicGuardState::Running&&e.budget.snapshot().attached);}
        e.wire.revoke(action);CHECK(currentRead(action,[&]{return guard.snapshot().state==EconomicGuardState::Stopped&&!guard.snapshot().point&&e.budget.snapshot().state!=EconomicGuardState::Running;}));
        CHECK(e.budget.snapshot().state!=EconomicGuardState::Running&&e.budget.snapshot().attached&&e.budget.snapshot().incarnations==1);
        CHECK(before.point&&study&&study->summary().bestPoint.has_value());
    }else{
        EconomicCommandPolicy p;p.enabled=true;p.guard=guardPolicy();p.commandLeaseMs=2000;
        EconomicCommandOwner owner(p,binding(),e.evaluation,e.wire.issuer,e.budget,e.wire.intent,e.wire.token);
        if(action=="foreign-start"){{NativeProcessScope old(foreignEpoch);CHECK(!owner.start(at(),e.in.currentPlan));}
            CHECK(e.wire.issuer.snapshot(at().monotonicMs).state==WorkerControlState::Active&&e.budget.snapshot().state==EconomicGuardState::Running&&!e.budget.snapshot().attached);return;}
        CHECK(owner.start(at(),e.in.currentPlan));e.study();const auto command=owner.poll(at(1),1,e.in.currentPlan,e.in.conditions,UINT64_MAX);
        CHECK(command&&command->directive==D::Run);if(!command)return;
        e.wire.now=at(2).monotonicMs;CHECK(e.wire.gate.receive(*command)==WorkerCommandResult::Accepted);unsigned installed=99;
        e.wire.gate.pauseBoundary(false);CHECK(e.wire.gate.applyWorkWait(installed)==WorkControlRequest::Run);const auto receipt=*e.wire.gate.snapshot().receipt;
        const auto before=owner.snapshot();CHECK(before.state==EconomicCommandState::Running&&before.pending&&before.lastRun&&!before.revokeRequired);
        if(action=="foreign"){{NativeProcessScope old(foreignEpoch);CHECK(owner.snapshot().state!=EconomicCommandState::Running&&owner.snapshot().revokeRequired&&!owner.snapshot().lastRun);
            CHECK(!owner.poll(at(900000),1,e.in.currentPlan,e.in.conditions,UINT64_MAX));CHECK(owner.acknowledge(receipt,at(900000))==R::Rejected);CHECK(!owner.retire(at(900000)));}
            CHECK(!owner.poll(at(2),1,e.in.currentPlan,e.in.conditions,UINT64_MAX));
            CHECK(owner.snapshot().state==EconomicCommandState::Running&&!owner.snapshot().revokeRequired&&owner.snapshot().pending&&owner.snapshot().lastRun==before.lastRun);
            CHECK(e.budget.snapshot().state==EconomicGuardState::Running&&e.budget.snapshot().attached);}
        e.wire.revoke(action);CHECK(currentRead(action,[&]{return owner.snapshot().state==EconomicCommandState::Stopped&&owner.snapshot().revokeRequired&&owner.snapshot().pending&&owner.snapshot().lastRun==before.lastRun&&e.budget.snapshot().state!=EconomicGuardState::Running;}));
        CHECK(owner.acknowledge(receipt,at(2))==R::Terminal&&!owner.snapshot().pending&&owner.snapshot().application);
        CHECK(e.budget.snapshot().state!=EconomicGuardState::Running&&e.budget.snapshot().attached);CHECK(before.lastRun->study&&before.pending);
    }
}
WorkProfilePlan measuredPlan(){auto p=profile()->input().plan;p.telemetryCadenceMs=5;p.maximumSampleGapMs=60;p.warmupMs=10;p.measurementMs=20;
    p.repetitions=2;p.maximumElapsedMs=1000;p.maximumEnergy.numerator=d("1000000");return p;}
WorkProfileSample sample(const Wire &w,const WorkProfilePlan &p,uint64_t ms=0){WorkProfileSample s;s.generation=w.token.epoch;s.counterEpoch=1;s.fingerprint=workProfileFingerprint(p.identity);
    s.utcMs=at(ms).utcMs;s.monotonicMs=at(ms).monotonicMs;s.completeWorkBoundary=s.safetyTelemetryValid=s.thermalSafe=s.gameStateKnown=true;
    s.milliwatts=200000;s.userTmacs=d(std::to_string(ms*2));s.donationTmacs=d(std::to_string(ms));return s;}
void measurement(const std::string &kind,const std::string &action,uint64_t foreignEpoch){
    Wire w;const auto p=measuredPlan();
    if(kind=="measure"){
        WorkMeasurementOwner owner(p,binding(),w.issuer,w.intent,w.token);
        if(action=="foreign-start"){{NativeProcessScope old(foreignEpoch);CHECK(!owner.start(sample(w,p),measureAt(),w.conditions));}
            CHECK(w.issuer.snapshot(at().monotonicMs).state==WorkerControlState::Active);return;}
        CHECK(owner.start(sample(w,p),measureAt(),w.conditions));const auto before=owner.snapshot();
        CHECK(before.state==WorkMeasurementState::AwaitingApplication&&before.requestedHostWaitMs==0);
        if(action=="foreign"){{NativeProcessScope old(foreignEpoch);CHECK(owner.snapshot().state==WorkMeasurementState::Stopped&&owner.snapshot().revokeRequired&&!owner.snapshot().requestedHostWaitMs);
            CHECK(owner.poll(measureAt(900000),w.conditions).state==WorkMeasurementState::Stopped);CHECK(!owner.append(2,sample(w,p,900000),measureAt(900000),w.conditions));}
            CHECK(owner.poll(measureAt(1),w.conditions).state==before.state);
            CHECK(owner.snapshot().state==before.state&&!owner.snapshot().revokeRequired&&owner.snapshot().sequence==1);CHECK(w.issuer.snapshot(at(1).monotonicMs).state==WorkerControlState::Active);}
        w.revoke(action);CHECK(currentRead(action,[&]{return owner.snapshot().state==WorkMeasurementState::Stopped&&owner.snapshot().revokeRequired&&!owner.snapshot().requestedHostWaitMs&&!owner.snapshot().sourceValidUntilMonoMs;}));
        CHECK(owner.snapshot().latestSample&&owner.snapshot().sequence==before.sequence&&before.requestedHostWaitMs==0);
    }else{
        WorkExperimentBudgetPolicy b;b.enabled=true;b.maximumAttempts=2;b.minimumRetestMs=0;b.maximumReservedElapsedMs=2000;b.maximumReservedEnergy.numerator=d("2000000");
        WorkExperimentBudget budget(b,p,w.intent,w.token);CHECK(budget.start(measureAt()));
        WorkExperimentOwner owner({true,100,5,500},p,binding(),w.issuer,w.intent,w.token,&budget);
        if(action=="foreign-start"){{NativeProcessScope old(foreignEpoch);CHECK(!owner.start(sample(w,p),measureAt(),w.conditions,UINT64_MAX));}
            CHECK(w.issuer.snapshot(at().monotonicMs).state==WorkerControlState::Active&&budget.snapshot().state==WorkExperimentBudgetState::Running&&!budget.snapshot().attached&&!budget.snapshot().attempts);return;}
        CHECK(owner.start(sample(w,p),measureAt(),w.conditions,UINT64_MAX));
        const auto command=owner.poll(measureAt(1),w.conditions,UINT64_MAX);CHECK(command&&command->directive==D::Run);if(!command)return;
        w.now=at(2).monotonicMs;CHECK(w.gate.receive(*command)==WorkerCommandResult::Accepted);unsigned installed=99;w.gate.pauseBoundary(false);
        CHECK(w.gate.applyWorkWait(installed)==WorkControlRequest::Run);const auto receipt=*w.gate.snapshot().receipt;
        const auto before=owner.snapshot();const auto reservation=budget.snapshot().latest;CHECK(before.pending&&before.lastRun&&reservation);
        if(action=="foreign"){{NativeProcessScope old(foreignEpoch);CHECK(owner.snapshot().state==WorkExperimentState::Stopped&&owner.snapshot().revokeRequired&&!owner.snapshot().lastRun);
            CHECK(owner.measurement().state==WorkMeasurementState::Stopped&&!owner.measurement().requestedHostWaitMs);
            CHECK(budget.snapshot().state==WorkExperimentBudgetState::Stopped&&!budget.snapshot().attached&&!budget.snapshot().latest);
            CHECK(!owner.poll(measureAt(900000),w.conditions,UINT64_MAX));CHECK(!owner.append(2,sample(w,p,900000),measureAt(900000),w.conditions,UINT64_MAX));
            CHECK(owner.acknowledge(receipt,measureAt(900000),w.conditions,UINT64_MAX)==R::Rejected);}
            CHECK(!owner.poll(measureAt(2),w.conditions,UINT64_MAX));
            CHECK(owner.snapshot().state==WorkExperimentState::Running&&!owner.snapshot().revokeRequired&&owner.snapshot().pending&&owner.snapshot().lastRun==before.lastRun);
            CHECK(budget.snapshot().state==WorkExperimentBudgetState::Running&&budget.snapshot().attached&&budget.snapshot().latest==reservation);}
        w.revoke(action);CHECK(currentRead(action,[&]{return owner.snapshot().state==WorkExperimentState::Stopped&&owner.snapshot().revokeRequired&&owner.snapshot().pending&&owner.snapshot().lastRun==before.lastRun&&!owner.snapshot().handoffUntilMonoMs&&owner.measurement().state==WorkMeasurementState::Stopped&&budget.snapshot().state==WorkExperimentBudgetState::Stopped;}));
        CHECK(owner.measurement().state==WorkMeasurementState::Stopped&&!owner.measurement().requestedHostWaitMs);
        CHECK(budget.snapshot().state==WorkExperimentBudgetState::Stopped&&budget.snapshot().attached&&budget.snapshot().attempts==1&&budget.snapshot().latest==reservation);
        CHECK(owner.acknowledge(receipt,measureAt(2),w.conditions,UINT64_MAX)==R::Terminal&&!owner.snapshot().pending);
        CHECK(reservation->elapsedMs==1000&&before.lastRun->source.monotonicMs==at().monotonicMs);
    }
}
void issuer(const std::string &action,uint64_t foreignEpoch){
    Wire w;
    if(action=="foreign-start"){WorkerControlIssuer other(binding(),w.intent,w.token);{NativeProcessScope old(foreignEpoch);CHECK(!other.start(w.now));}
        CHECK(w.issuer.snapshot(w.now).state==WorkerControlState::Active);return;}
    const auto c=w.issuer.issue(D::Run,at(1).monotonicMs,100,0);CHECK(c.has_value());if(!c)return;
    const WorkerReceipt receipt{*c,K::WaitApplied,at(2).monotonicMs};
    if(action=="foreign"){{NativeProcessScope old(foreignEpoch);const auto denied=w.issuer.snapshot(at(900000).monotonicMs);
        CHECK(denied.state==WorkerControlState::Stopped&&!denied.pending&&!denied.settled);
        CHECK(w.issuer.acknowledge(receipt,at(900000).monotonicMs)==R::Rejected);CHECK(!w.issuer.issue(D::Run,at(900000).monotonicMs,100,0));CHECK(!w.issuer.paused(at(900000).monotonicMs)&&!w.issuer.appliedWait(at(900000).monotonicMs));}
        const auto current=w.issuer.snapshot(at(2).monotonicMs);CHECK(current.state==WorkerControlState::Active&&current.pending&&sameWorkerCommand(*current.pending,*c));}
    w.revoke(action);CHECK(w.issuer.snapshot(at(2).monotonicMs).state==WorkerControlState::Stopped&&w.issuer.snapshot(at(2).monotonicMs).pending);
    CHECK(w.issuer.acknowledge(receipt,at(2).monotonicMs)==R::Terminal&&!w.issuer.snapshot(at(2).monotonicMs).pending);
}
void schedule(const std::string &action,uint64_t foreignEpoch){
    SessionIntent intent;const auto token=intent.explicitStart();OptimizationSchedulePolicy p;p.enabled=true;
    OptimizationSchedule owner(p,intent,token);
    if(action=="foreign-start"){{NativeProcessScope old(foreignEpoch);CHECK(!owner.start(at()));}CHECK(!owner.start(at())&&owner.snapshot().state==OptimizationScheduleState::Stopped);return;}
    CHECK(owner.start(at()));const auto requests=owner.poll(at());
    CHECK(requests.requests[0]&&requests.requests[1]);if(!requests.requests[0]||!requests.requests[1])return;
    if(action=="foreign"){{NativeProcessScope old(foreignEpoch);CHECK(owner.snapshot().state==OptimizationScheduleState::Stopped&&!owner.snapshot().lanes[0].pending);
        const auto denied=owner.poll(at(900000));CHECK(denied.state==OptimizationScheduleState::Stopped&&!denied.requests[0]&&!denied.requests[1]&&!denied.cancellations[0]&&!denied.cancellations[1]);
        CHECK(owner.complete(*requests.requests[0],OptimizationOutcome::Complete,at(900000))!=OptimizationCompletion::Accepted);}
        const auto current=owner.poll(at(1));CHECK(current.state==OptimizationScheduleState::Running&&!current.requests[0]&&!current.requests[1]&&!current.cancellations[0]&&!current.cancellations[1]);
        CHECK(owner.snapshot().state==OptimizationScheduleState::Running&&owner.snapshot().lanes[0].pending&&owner.snapshot().lanes[1].pending&&!owner.snapshot().lanes[0].cancellationRequested);}
    if(action=="intent-stop")intent.stop();else fault();
    CHECK(currentRead(action,[&]{return owner.snapshot().state==OptimizationScheduleState::Stopped&&owner.snapshot().lanes[0].pending&&owner.snapshot().lanes[1].pending&&owner.snapshot().lanes[0].cancellationRequested&&owner.snapshot().lanes[1].cancellationRequested;}));
    CHECK(owner.complete(*requests.requests[0],OptimizationOutcome::Unreaped,at(2))==OptimizationCompletion::Stopped);
    CHECK(owner.snapshot().unreaped&&!owner.snapshot().lanes[0].pending&&owner.snapshot().lanes[1].pending);
}
}
bool canaryNativeAllocationFault(){if(!rejectAllocations.load())return false;++allocationAttempts;return true;}
int main(int argc,char **argv){
    if(argc!=3)return 4;
    const std::string kind=argv[1],action=argv[2];
    if((kind!="command"&&kind!="guard"&&kind!="measure"&&kind!="experiment"&&kind!="issuer"&&kind!="schedule")||(action!="fault"&&action!="intent-stop"&&action!="foreign"&&action!="foreign-start"&&action!="allocation")||(kind=="issuer"&&action=="allocation"))return 4;
    const auto original=nativeProcessEpoch();if(action=="foreign"||action=="foreign-start"){
        fault();SessionIntent probe;const auto token=probe.explicitStart();CHECK(probe.permits(token)&&!probe.foreignCaller(token));
        {NativeProcessScope old(original);CHECK(probe.foreignCaller(token)&&!probe.permits(token));CHECK(!probe.foreignCaller({})&&!probe.foreignCaller({token.epoch+1}));
            {NativeProcessScope conflict(nativeProcessEpoch());CHECK(probe.foreignCaller(token)&&!probe.permits(token));}}
        CHECK(probe.permits(token)&&!probe.foreignCaller(token));probe.stop();{NativeProcessScope old(original);CHECK(!probe.foreignCaller(token)&&!probe.permits(token));}
    }
    if(kind=="command"||kind=="guard")economic(kind,action,original);else if(kind=="issuer")issuer(action,original);else if(kind=="schedule")schedule(action,original);else measurement(kind,action,original);
    CHECK(!nativeProcessUnresolved());return finishTests("control current authority, original scope and historical ownership");
}
