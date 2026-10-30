#include "economic_command_owner.h"
#include "work_experiment_owner.h"
#include <algorithm>
namespace om {
namespace {
using S=EconomicCommandState;using G=EconomicGuardState;using A=EconomicGuardAction;
bool same(const WorkerReceipt &a,const WorkerReceipt &b){return sameWorkerCommand(a.command,b.command)&&a.kind==b.kind&&a.observedMonoMs==b.observedMonoMs;}
bool safe(const WorkProfileConditions &c){return c.safetyTelemetryValid&&c.thermalSafe&&c.gameStateKnown&&!c.gameActive&&!c.fault&&!c.drift;}
}
struct EconomicCommandOwner::Impl {
    EconomicCommandPolicy policy;const EconomicEvaluationOwner &evaluation;WorkerControlIssuer &issuer;
    const SessionIntent &intent;SessionToken token;EconomicChoiceGuard guard;EconomicCommandSnapshot view;
    bool attempted=false;OptimizationTime anchor,last;std::string planKey;
    std::optional<WorkerCommand> flight;std::optional<WorkerReceipt> settled;
    std::unique_ptr<EconomicStudyUse> pendingUse;uint64_t pendingCeiling=0;
    Impl(EconomicCommandPolicy p,WorkerControlBinding b,const EconomicEvaluationOwner &e,WorkerControlIssuer &w,EconomicParentBudget &budget,const SessionIntent &i,SessionToken t)
        :policy(std::move(p)),evaluation(e),issuer(w),intent(i),token(t),guard(policy.guard,std::move(b),e,w,i,t,&budget){}
    void halt(S state){if(view.state==S::Retired)return;
        if(view.state!=S::Faulted&&view.state!=S::ClockFault&&view.state!=S::Stopped)view.state=state;
        attempted=true;view.revokeRequired=true;issuer.requestStop();guard.requestStop();}
    bool clock(OptimizationTime now){
        if(now.utcMs<0||now.utcMs>253402300799999LL||!now.monotonicMs||now.utcMs<last.utcMs||now.monotonicMs<last.monotonicMs){halt(S::ClockFault);return false;}
        const auto wall=static_cast<uint64_t>(now.utcMs-anchor.utcMs),mono=now.monotonicMs-anchor.monotonicMs;
        if((wall>mono?wall-mono:mono-wall)>2000){halt(S::ClockFault);return false;}
        last=now;if(!intent.permits(token))halt(S::Stopped);return true;
    }
    bool exact(const WorkerControlIssuerSnapshot &s)const{
        if(flight&&view.pending)return s.pending&&sameWorkerCommand(*s.pending,*flight);
        return !s.pending&&s.settled&&settled&&same(*s.settled,*settled);
    }
    bool runCovered(const WorkerCommand &command,OptimizationTime now,uint64_t revision,const WorkProfilePlan &plan,const WorkProfileConditions &conditions,uint64_t permission){
        if(command.directive==WorkerDirective::Pause)return true;
        const auto &r=view.lastRun;const auto &source=evaluation.snapshot();
        if(!r||!pendingUse||!safe(conditions)||revision!=r->decision.contextRevision||permission<command.deadlineMonoMs)return false;
        uint64_t ceiling=0;
        if(source.study==r->study&&source.resultSequence==r->decision.resultSequence)ceiling=source.use.validUntilMonoMs;
        else if(source.pending&&!source.cancellationRequested&&source.retainedResultSequence==r->decision.resultSequence)ceiling=source.retainedValidUntilMonoMs;
        else return false;
        const auto &use=pendingUse->check(plan,now,conditions);
        if(use.state!=EconomicUseState::Eligible)return false;
        pendingCeiling=(std::min)({pendingCeiling,ceiling,use.validUntilMonoMs});
        return pendingCeiling>=command.deadlineMonoMs;
    }
};
EconomicCommandOwner::EconomicCommandOwner(EconomicCommandPolicy p,WorkerControlBinding b,const EconomicEvaluationOwner &e,WorkerControlIssuer &w,
    EconomicParentBudget &budget,const SessionIntent &i,SessionToken t):impl_(new Impl(std::move(p),std::move(b),e,w,budget,i,t)){}
EconomicCommandOwner::~EconomicCommandOwner(){if(impl_->view.state==S::Running)requestStop();}
bool EconomicCommandOwner::start(OptimizationTime now,const WorkProfilePlan &plan){auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    if(!p.intent.permits(p.token)){p.halt(S::Stopped);return false;}
    if(!p.policy.enabled){p.view.state=S::Disabled;return false;}
    p.anchor=p.last=now;
    try{if(!p.clock(now))return false;
        if(p.policy.commandLeaseMs<100||p.policy.commandLeaseMs>workerControlMaxLeaseMs||!p.policy.guard.enabled||!p.guard.start(now,plan)){p.halt(S::Faulted);return false;}
        p.planKey=workProfilePlanFingerprint(plan);p.settled=p.issuer.snapshot(now.monotonicMs).settled;p.view.commandRevision=1;p.view.state=S::Running;return true;
    }catch(...){p.halt(S::Faulted);return false;}}
bool EconomicCommandOwner::startAfterMeasurement(OptimizationTime now,const WorkProfilePlan &plan,WorkExperimentOwner &measurement){auto &p=*impl_;
    if(p.attempted)return false;
    p.attempted=true;
    if(!p.intent.permits(p.token)){p.halt(S::Stopped);return false;}
    if(!p.policy.enabled){p.view.state=S::Disabled;return false;}
    try{WorkerReceipt paused;
        const auto record=measurement.release(p.issuer,p.intent,p.token,{now.utcMs,now.monotonicMs},plan,&paused);
        if(!record){p.halt(S::Faulted);return false;}
        p.anchor={record->measurement->admitted.utcMs,record->measurement->admitted.monotonicMs};p.last=now;
        if(!p.clock(now)||p.policy.commandLeaseMs<100||p.policy.commandLeaseMs>workerControlMaxLeaseMs||!p.policy.guard.enabled||
            !p.guard.startMeasured(now,plan,*record->measurement,paused)){p.halt(S::Faulted);return false;}
        p.planKey=workProfilePlanFingerprint(plan);p.settled=paused;p.view.commandRevision=paused.command.revision;
        p.view.measurement=record;p.view.state=S::Running;return true;
    }catch(...){p.halt(S::Faulted);return false;}}
std::optional<WorkerCommand> EconomicCommandOwner::poll(OptimizationTime now,uint64_t revision,const WorkProfilePlan &plan,const WorkProfileConditions &conditions,uint64_t permission){auto &p=*impl_;
    if(p.view.state!=S::Running)return {};
    try{if(!p.clock(now)||p.view.state!=S::Running)return {};
        if(workProfilePlanFingerprint(plan)!=p.planKey){p.halt(S::Faulted);return {};}
        const auto worker=p.issuer.snapshot(now.monotonicMs);
        if(worker.state!=WorkerControlState::Active||!p.exact(worker)){p.halt(worker.state==WorkerControlState::Stopped?S::Stopped:worker.state==WorkerControlState::ClockFault?S::ClockFault:S::Faulted);return {};}
        const auto decision=p.guard.check(now,revision,plan,conditions);
        if(decision.state!=G::Running){p.halt(decision.state==G::Stopped?S::Stopped:decision.state==G::ClockFault?S::ClockFault:S::Faulted);return {};}
        if(p.view.pending){if(!p.runCovered(*p.flight,now,revision,plan,conditions,permission))p.halt(S::Faulted);return {};}
        p.flight.reset();
        if(!p.settled||now.monotonicMs>UINT64_MAX-p.policy.commandLeaseMs){p.halt(S::Faulted);return {};}
        const auto &previous=p.settled->command;
        const bool covered=p.runCovered(previous,now,revision,plan,conditions,permission);
        const bool sourceChanged=previous.directive==WorkerDirective::Run&&p.view.lastRun&&
            (decision.contextRevision!=p.view.lastRun->decision.contextRevision||decision.resultSequence!=p.view.lastRun->decision.resultSequence);
        const auto headroom=(std::min)(uint64_t{100},p.policy.commandLeaseMs/2);
        auto directive=WorkerDirective::Pause;std::optional<unsigned> wait;uint64_t deadline=now.monotonicMs+p.policy.commandLeaseMs;
        const bool run=decision.action==A::Hold||decision.action==A::Resume||decision.action==A::Switch;
        if(run&&decision.validUntilMonoMs>now.monotonicMs&&decision.point&&decision.hostWaitMs&&permission>now.monotonicMs){
            const auto ceiling=(std::min)({deadline,permission,decision.sourceValidUntilMonoMs});
            if(ceiling>now.monotonicMs&&ceiling-now.monotonicMs>headroom){directive=WorkerDirective::Run;wait=decision.hostWaitMs;deadline=ceiling;}
        }else if(decision.action==A::None&&covered&&safe(conditions)&&permission>=previous.deadlineMonoMs&&previous.deadlineMonoMs-now.monotonicMs>headroom)return {};
        if(previous.directive==directive&&(directive==WorkerDirective::Pause||previous.hostWaitMs==wait)&&
            previous.deadlineMonoMs-now.monotonicMs>p.policy.commandLeaseMs/2&&(directive==WorkerDirective::Pause||(!sourceChanged&&deadline>=previous.deadlineMonoMs)))return {};
        std::shared_ptr<EconomicRunBinding> original;
        if(directive==WorkerDirective::Run){const auto study=p.guard.commandStudy();
            if(!study||*decision.point>=plan.hostWaitMs.size()||plan.hostWaitMs[*decision.point]!=*decision.hostWaitMs||
                std::none_of(study->summary().candidates.begin(),study->summary().candidates.end(),[&](const EconomicCandidate &c){return c.point==*decision.point&&c.hostWaitMs==*decision.hostWaitMs;})){p.halt(S::Faulted);return {};}
            original=std::make_shared<EconomicRunBinding>();original->selected=now;original->decision=decision;original->policy=p.policy.guard;
            original->study=study;original->safetyGameDeadlineMonoMs=permission;
            p.pendingUse=std::make_unique<EconomicStudyUse>(study,p.policy.guard.result,p.intent,p.token);p.pendingCeiling=decision.sourceValidUntilMonoMs;
        }
        const auto command=p.issuer.issue(directive,now.monotonicMs,deadline-now.monotonicMs,wait);
        if(!command){p.halt(S::Faulted);return {};}
        p.flight=command;p.view.pending=true;p.view.commandRevision=command->revision;
        if(original){original->command=*command;p.view.lastRun=std::move(original);p.view.application.reset();}
        else{p.pendingUse.reset();p.pendingCeiling=0;}
        return command;
    }catch(...){p.halt(S::Faulted);return {};}}
WorkerReceiptResult EconomicCommandOwner::acknowledge(const WorkerReceipt &r,OptimizationTime now){auto &p=*impl_;
    if(!p.attempted||p.view.state==S::Dormant||p.view.state==S::Disabled||p.view.state==S::Retired)return WorkerReceiptResult::Rejected;
    try{if(!p.clock(now))return WorkerReceiptResult::Rejected;
        if(!(p.settled&&same(r,*p.settled))&&(!p.flight||!sameWorkerCommand(r.command,*p.flight))){p.halt(S::Faulted);return WorkerReceiptResult::Rejected;}
        const auto result=p.issuer.acknowledge(r,now.monotonicMs);
        if(result==WorkerReceiptResult::Rejected){p.halt(S::Faulted);return result;}
        if(r.kind==WorkerReceiptKind::Paused||r.kind==WorkerReceiptKind::WaitApplied){
            // A duplicate historical receipt must not settle a newer flight.
            if(p.flight&&sameWorkerCommand(r.command,*p.flight)){p.settled=r;p.view.pending=false;
                if(r.kind==WorkerReceiptKind::WaitApplied&&p.view.lastRun&&sameWorkerCommand(r.command,p.view.lastRun->command))p.view.application=r;}
        }
        if(result==WorkerReceiptResult::Terminal&&p.view.state==S::Running)p.halt(S::Faulted);
        return result;
    }catch(...){p.halt(S::Faulted);return WorkerReceiptResult::Rejected;}}
void EconomicCommandOwner::transportFailed(){impl_->halt(S::Faulted);}
void EconomicCommandOwner::requestStop(){impl_->halt(S::Stopped);}
bool EconomicCommandOwner::retire(OptimizationTime now){auto &p=*impl_;if(p.view.state!=S::Running)return false;
    try{if(!p.clock(now)||p.view.state!=S::Running)return false;
        const auto worker=p.issuer.snapshot(now.monotonicMs);
        if(p.view.pending||worker.state!=WorkerControlState::Stopped||!p.exact(worker)||!p.guard.retire(now)){p.halt(S::Faulted);return false;}
        p.view.state=S::Retired;p.view.revokeRequired=true;p.flight.reset();p.pendingUse.reset();return true;
    }catch(...){p.halt(S::Faulted);return false;}}
const EconomicCommandSnapshot &EconomicCommandOwner::snapshot()const{return impl_->view;}
} // namespace om
