#include "work_experiment_owner.h"
#include "evidence_deadline.h"
#include <algorithm>
namespace om {
namespace {
using S=WorkExperimentState;using M=WorkMeasurementState;using R=WorkProfileStatus;
using D=WorkerDirective;using K=WorkerReceiptKind;
bool live(S s){return s==S::Running||s==S::Holding;}
bool same(const WorkerReceipt &a,const WorkerReceipt &b){return sameWorkerCommand(a.command,b.command)&&a.kind==b.kind&&a.observedMonoMs==b.observedMonoMs;}
GameGuardTime time(WorkMeasurementTime t){return {t.utcMs,t.monotonicMs};}
}
struct WorkExperimentOwner::Impl {
    WorkExperimentPolicy policy;std::shared_ptr<const WorkProfilePlan> plan;WorkerControlIssuer &issuer;
    const SessionIntent &intent;SessionToken token;WorkMeasurementOwner capture;WorkExperimentSnapshot view;
    bool attempted=false;WorkMeasurementTime anchor,last,baseline;std::optional<WorkerCommand> flight;
    EvidenceDeadline budgetExpiry,runExpiry,handoffExpiry;
    std::vector<std::shared_ptr<const WorkExperimentRunBinding>> runs;
    Impl(WorkExperimentPolicy p,WorkProfilePlan f,WorkerControlBinding b,WorkerControlIssuer &w,const SessionIntent &i,SessionToken t)
        :policy(p),plan(std::make_shared<const WorkProfilePlan>(std::move(f))),issuer(w),intent(i),token(t),capture(*plan,std::move(b),w,i,t){}
    void halt(R r){if(view.state==S::Transferred)return;
        attempted=true;if(view.state!=S::Stopped&&view.state!=S::Faulted){view.state=r==R::Stopped?S::Stopped:S::Faulted;view.status=r;}
        view.revokeRequired=true;view.handoffUntilMonoMs=0;capture.requestStop();issuer.requestStop();}
    bool clock(WorkMeasurementTime now){
        if(now.utcMs<0||now.utcMs>253402300799999LL||!now.monotonicMs||now.utcMs<last.utcMs||now.monotonicMs<last.monotonicMs){halt(R::Clock);return false;}
        const auto wall=static_cast<uint64_t>(now.utcMs-anchor.utcMs),mono=now.monotonicMs-anchor.monotonicMs;
        if((wall>mono?wall-mono:mono-wall)>2000){halt(R::Clock);return false;}last=now;return true;
    }
    bool exact(const WorkerControlIssuerSnapshot &s)const{
        return s.settled&&view.receipt&&same(*s.settled,*view.receipt)&&
            (view.pending?(s.pending&&flight&&sameWorkerCommand(*s.pending,*flight)):!s.pending);
    }
    bool check(WorkMeasurementTime now,const WorkProfileConditions &c,uint64_t permission){
        if(!live(view.state))return false;
        if(!intent.permits(token)){halt(R::Stopped);return false;}
        if(!clock(now))return false;
        const auto worker=issuer.snapshot(now.monotonicMs);
        if(worker.state!=WorkerControlState::Active||!exact(worker)){halt(worker.state==WorkerControlState::ClockFault?R::Clock:
            worker.state==WorkerControlState::Stopped?R::Stopped:R::Attribution);return false;}
        if(c.drift){halt(R::Drift);return false;}
        if(c.fault||!c.safetyTelemetryValid||!c.thermalSafe||permission<=now.monotonicMs){halt(R::Unsafe);return false;}
        if(!c.gameStateKnown||c.gameActive){halt(R::Game);return false;}
        if(view.state==S::Holding){const auto completed=view.record->measurement->completed;
            view.handoffUntilMonoMs=handoffExpiry.clip(1,time(now),time(completed),policy.handoffTimeoutMs);
            if(!view.handoffUntilMonoMs){halt(R::Stale);return false;}return true;}
        const auto &s=capture.poll(now,c);
        if(s.revokeRequired){halt(s.status);return false;}
        const auto budget=budgetExpiry.clip(1,time(now),time(baseline),plan->maximumElapsedMs);
        if(!budget){halt(R::Budget);return false;}
        if(!s.sourceValidUntilMonoMs){halt(R::Coverage);return false;}
        const auto &command=view.pending?*flight:view.receipt->command;
        if(command.directive==D::Run){const auto &original=view.lastRun;
            if(!original||!sameWorkerCommand(command,original->command)){halt(R::Attribution);return false;}
            const auto source=runExpiry.clip(original->sequence,time(now),{original->source.utcMs,original->source.monotonicMs},plan->maximumSampleGapMs);
            if((std::min)({source,budget,permission})<command.deadlineMonoMs){halt(R::Stale);return false;}}
        return true;
    }
};
WorkExperimentOwner::WorkExperimentOwner(WorkExperimentPolicy p,WorkProfilePlan f,WorkerControlBinding b,WorkerControlIssuer &w,const SessionIntent &i,SessionToken t)
    :impl_(new Impl(p,std::move(f),std::move(b),w,i,t)){}
WorkExperimentOwner::~WorkExperimentOwner(){if(live(impl_->view.state))requestStop();}
bool WorkExperimentOwner::start(WorkProfileSample sample,WorkMeasurementTime now,const WorkProfileConditions &c,uint64_t permission){auto &p=*impl_;
    if(p.attempted)return false;
    p.attempted=true;
    if(!p.intent.permits(p.token)){p.halt(R::Stopped);return false;}
    if(!p.policy.enabled){p.view.state=S::Disabled;p.view.status=R::Disabled;return false;}
    try{if(p.policy.commandLeaseMs<100||p.policy.commandLeaseMs>workerControlMaxLeaseMs||!p.policy.renewalMarginMs||
            p.policy.renewalMarginMs>p.policy.commandLeaseMs/2||!p.policy.handoffTimeoutMs||p.policy.handoffTimeoutMs>60000){p.halt(R::Invalid);return false;}
        p.anchor=p.last=now;p.baseline={sample.utcMs,sample.monotonicMs};
        if(!p.clock(now)||!p.capture.start(std::move(sample),now,c)){p.halt(p.capture.snapshot().status);return false;}
        p.view.receipt=p.issuer.snapshot(now.monotonicMs).settled;p.view.commandRevision=1;p.view.state=S::Running;p.view.status=R::Ok;
        return p.check(now,c,permission);
    }catch(...){p.halt(R::Capacity);return false;}}
bool WorkExperimentOwner::append(uint64_t sequence,WorkProfileSample sample,WorkMeasurementTime now,const WorkProfileConditions &c,uint64_t permission){auto &p=*impl_;
    if(p.view.state!=S::Running)return false;
    try{if(!p.check(now,c,permission))return false;
        if(!p.capture.append(sequence,std::move(sample),now,c)){p.halt(p.capture.snapshot().status);return false;}
        if(p.capture.snapshot().state==M::Complete){
            if(p.view.pending||!p.view.receipt||p.view.receipt->kind!=K::Paused){p.halt(R::Attribution);return false;}
            p.view.record=std::make_shared<const WorkExperimentRecord>(WorkExperimentRecord{p.capture.snapshot().record,std::move(p.runs)});
            p.view.state=S::Holding;
        }
        return p.check(now,c,permission);
    }catch(...){p.halt(R::Capacity);return false;}}
std::optional<WorkerCommand> WorkExperimentOwner::poll(WorkMeasurementTime now,const WorkProfileConditions &c,uint64_t permission){auto &p=*impl_;
    try{if(!p.check(now,c,permission)||p.view.pending)return {};
        if(now.monotonicMs>UINT64_MAX-p.policy.commandLeaseMs){p.halt(R::Clock);return {};}
        const auto &s=p.capture.snapshot();const auto &previous=p.view.receipt->command;
        const auto wait=p.view.state==S::Running?s.requestedHostWaitMs:std::nullopt;
        const auto directive=wait?D::Run:D::Pause;uint64_t deadline=now.monotonicMs+p.policy.commandLeaseMs;
        if(wait){deadline=(std::min)({deadline,permission,s.sourceValidUntilMonoMs,
                p.budgetExpiry.clip(1,time(now),time(p.baseline),p.plan->maximumElapsedMs)});
            if(deadline<=now.monotonicMs||deadline-now.monotonicMs<=p.policy.renewalMarginMs)return {};}
        if(previous.directive==directive&&previous.hostWaitMs==wait&&previous.deadlineMonoMs-now.monotonicMs>p.policy.renewalMarginMs&&
            (directive==D::Pause||deadline>=previous.deadlineMonoMs))return {};
        if(p.view.commandRevision>=8192){p.halt(R::Capacity);return {};}
        std::shared_ptr<WorkExperimentRunBinding> original;
        if(wait){original=std::make_shared<WorkExperimentRunBinding>();original->selected=now;original->plan=p.plan;original->source=*s.latestSample;
            original->sequence=s.sequence;original->requestRevision=s.requestRevision;original->sourceValidUntilMonoMs=s.sourceValidUntilMonoMs;
            original->measurementDeadlineMonoMs=p.budgetExpiry.clip(1,time(now),time(p.baseline),p.plan->maximumElapsedMs);original->safetyGameDeadlineMonoMs=permission;}
        const auto command=p.issuer.issue(directive,now.monotonicMs,deadline-now.monotonicMs,wait);
        if(!command){p.halt(R::Attribution);return {};}
        p.flight=command;p.view.pending=true;p.view.commandRevision=command->revision;
        if(original){original->command=*command;p.runs.push_back(original);p.view.lastRun=std::move(original);
            // A new command rebinds to its captured original; cached polls of that original still cannot extend it.
            p.runExpiry=EvidenceDeadline{};
            p.runExpiry.clip(p.view.lastRun->sequence,time(now),{p.view.lastRun->source.utcMs,p.view.lastRun->source.monotonicMs},p.plan->maximumSampleGapMs);}
        return command;
    }catch(...){p.halt(R::Capacity);return {};}}
WorkerReceiptResult WorkExperimentOwner::acknowledge(const WorkerReceipt &r,WorkMeasurementTime now,const WorkProfileConditions &c,uint64_t permission){auto &p=*impl_;
    if(!p.attempted||p.view.state==S::Dormant||p.view.state==S::Disabled||p.view.state==S::Transferred)return WorkerReceiptResult::Rejected;
    try{if(!p.clock(now))return WorkerReceiptResult::Rejected;
        if(live(p.view.state))p.check(now,c,permission);
        if(!(p.view.receipt&&same(*p.view.receipt,r))&&(!p.flight||!sameWorkerCommand(*p.flight,r.command))){p.halt(R::Attribution);return WorkerReceiptResult::Rejected;}
        const auto result=p.issuer.acknowledge(r,now.monotonicMs);
        if(result==WorkerReceiptResult::Rejected){p.halt(R::Attribution);return result;}
        if(p.flight&&sameWorkerCommand(*p.flight,r.command)&&r.kind!=K::Accepted){p.view.receipt=r;p.view.pending=false;}
        if(result==WorkerReceiptResult::Terminal&&live(p.view.state))p.halt(R::Unsafe);
        if(live(p.view.state))p.check(now,c,permission);
        return result;
    }catch(...){p.halt(R::Capacity);return WorkerReceiptResult::Rejected;}}
void WorkExperimentOwner::transportFailed(){impl_->halt(R::Unsafe);}
void WorkExperimentOwner::requestStop(){impl_->halt(R::Stopped);}
const WorkExperimentSnapshot &WorkExperimentOwner::snapshot()const{return impl_->view;}
const WorkMeasurementSnapshot &WorkExperimentOwner::measurement()const{return impl_->capture.snapshot();}
std::shared_ptr<const WorkExperimentRecord> WorkExperimentOwner::release(WorkerControlIssuer &issuer,const SessionIntent &intent,SessionToken token,
    WorkMeasurementTime now,const WorkProfilePlan &plan,WorkerReceipt *receipt){auto &p=*impl_;
    if(p.view.state!=S::Holding)return {};
    try{if(!p.clock(now))return {};
        const auto current=p.issuer.snapshot(now.monotonicMs);const auto completed=p.view.record->measurement->completed;
        if(&issuer!=&p.issuer||&intent!=&p.intent||token.epoch!=p.token.epoch||!p.intent.permits(p.token)||
            workProfilePlanFingerprint(plan)!=workProfilePlanFingerprint(*p.plan)||p.view.pending||!p.exact(current)||
            current.state!=WorkerControlState::Active||p.view.receipt->kind!=K::Paused||!receipt||
            !p.handoffExpiry.clip(1,time(now),time(completed),p.policy.handoffTimeoutMs)){p.halt(R::Attribution);return {};}
        *receipt=*p.view.receipt;p.view.state=S::Transferred;p.view.handoffUntilMonoMs=0;return p.view.record;
    }catch(...){p.halt(R::Capacity);return {};}}
} // namespace om
