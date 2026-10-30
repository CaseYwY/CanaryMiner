#include "work_experiment_budget.h"
#include <algorithm>
namespace om {
namespace {
using S=WorkExperimentBudgetState;using R=WorkProfileStatus;
constexpr uint64_t day=86400000;
bool positive(const ElectricityAmount &e){return !e.numerator.negative()&&!e.numerator.zero()&&e.numerator.digits()<=38&&e.numerator.scale()<=12;}
bool binding(const WorkerControlBinding &a,const WorkerControlBinding &b){return a.sessionKey==b.sessionKey&&a.workerKey==b.workerKey&&
    a.epoch==b.epoch&&a.slot==b.slot&&a.workerGeneration==b.workerGeneration;}
}
struct WorkExperimentBudget::Impl {
    WorkExperimentBudgetPolicy policy;WorkProfilePlan plan;const SessionIntent &intent;SessionToken token;WorkExperimentBudgetSnapshot view;
    bool attempted=false;WorkMeasurementTime anchor,last;uint64_t retiredAt=0;
    const void *borrower=nullptr,*issuer=nullptr,*transferredOwner=nullptr;
    std::vector<std::shared_ptr<const WorkExperimentReservation>> originals;std::optional<WorkerReceipt> handedPause;
    Impl(WorkExperimentBudgetPolicy p,WorkProfilePlan f,const SessionIntent &i,SessionToken t):policy(std::move(p)),plan(std::move(f)),intent(i),token(t){}
    void halt(R r){attempted=true;if(view.state==S::Stopped||view.state==S::Faulted)return;
        view.state=r==R::Stopped?S::Stopped:S::Faulted;view.status=r;}
    bool time(WorkMeasurementTime now){
        if(!intent.permits(token)){halt(R::Stopped);return false;}
        if(view.state!=S::Running)return false;
        if(now.utcMs<last.utcMs||now.utcMs>253402300799999LL||now.monotonicMs<last.monotonicMs){halt(R::Clock);return false;}
        const auto wall=static_cast<uint64_t>(now.utcMs-anchor.utcMs),mono=now.monotonicMs-anchor.monotonicMs;
        if((wall>mono?wall-mono:mono-wall)>2000){halt(R::Clock);return false;}
        last=now;return true;
    }
};
WorkExperimentBudget::WorkExperimentBudget(WorkExperimentBudgetPolicy p,WorkProfilePlan f,const SessionIntent &i,SessionToken t)
    :impl_(new Impl(std::move(p),std::move(f),i,t)){}
WorkExperimentBudget::~WorkExperimentBudget()=default;
bool WorkExperimentBudget::start(WorkMeasurementTime now){auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    if(!p.intent.permits(p.token)){p.halt(R::Stopped);return false;}
    if(!p.policy.enabled){p.view.state=S::Disabled;p.view.status=R::Disabled;return false;}
    try{if(!p.policy.maximumAttempts||p.policy.maximumAttempts>256||p.policy.minimumRetestMs>30*day||!p.policy.maximumReservedElapsedMs||
        p.policy.maximumReservedElapsedMs>256*day||!positive(p.policy.maximumReservedEnergy)||validateWorkProfilePlan(p.plan)!=R::Ok){p.halt(R::Invalid);return false;}
    if(now.utcMs<0||now.utcMs>253402300799999LL||!now.monotonicMs){p.halt(R::Clock);return false;}
    p.anchor=p.last=now;p.view.state=S::Running;p.view.status=R::Ok;return true;
    }catch(...){p.halt(R::Capacity);return false;}
}
WorkProfileStatus WorkExperimentBudget::admit(const void *owner,WorkerControlIssuer &issuer,const SessionIntent &intent,SessionToken token,
    const WorkProfilePlan &plan,WorkMeasurementTime now){auto &p=*impl_;
    try{if(!p.time(now))return p.view.status;
        const auto s=issuer.snapshot(now.monotonicMs);
        if(!owner||p.view.attached||&intent!=&p.intent||token.epoch!=p.token.epoch||!issuer.boundTo(p.intent,p.token)||
            workProfilePlanFingerprint(plan)!=workProfilePlanFingerprint(p.plan)||s.state!=WorkerControlState::Active||s.pending||!s.settled||
            s.settled->kind!=WorkerReceiptKind::Paused||s.settled->command.revision!=1){p.halt(R::Attribution);return p.view.status;}
        const auto &b=s.settled->command.binding;
        if((!p.view.latest&&(b.workerGeneration!=1||s.settled->command.issuedMonoMs<p.anchor.monotonicMs))||
            (p.view.latest&&(b.sessionKey!=p.view.latest->initialPause.command.binding.sessionKey||b.slot!=p.view.latest->initialPause.command.binding.slot||
                b.workerGeneration<=p.view.latest->initialPause.command.binding.workerGeneration||s.settled->command.issuedMonoMs<p.retiredAt))||
            std::any_of(p.originals.begin(),p.originals.end(),[&](const auto &r){return r->initialPause.command.binding.workerKey==b.workerKey;})){
            p.halt(R::Attribution);return p.view.status;}
        ExactDecimal reserved;
        if(p.view.attempts>=p.policy.maximumAttempts||p.plan.maximumElapsedMs>p.policy.maximumReservedElapsedMs-p.view.reservedElapsedMs||
            (p.view.latest&&now.monotonicMs-p.view.latest->admitted.monotonicMs<p.policy.minimumRetestMs))return R::Budget;
        if(!p.view.reservedEnergy.numerator.add(p.plan.maximumEnergy.numerator,&reserved)){p.halt(R::Capacity);return p.view.status;}
        if(reserved.compare(p.policy.maximumReservedEnergy.numerator)>0)return R::Budget;
        auto original=std::make_shared<const WorkExperimentReservation>(WorkExperimentReservation{*s.settled,now,p.view.attempts+1ULL,p.plan.maximumElapsedMs,p.plan.maximumEnergy});
        p.originals.push_back(original);p.view.latest=std::move(original);++p.view.attempts;
        p.view.reservedElapsedMs+=p.plan.maximumElapsedMs;p.view.reservedEnergy.numerator=std::move(reserved);
        p.view.attached=true;p.view.completed=p.view.transferred=false;p.borrower=owner;p.issuer=&issuer;p.transferredOwner=nullptr;p.handedPause.reset();return R::Ok;
    }catch(...){p.halt(R::Capacity);return p.view.status;}}
WorkProfileStatus WorkExperimentBudget::check(const void *owner,WorkMeasurementTime now){auto &p=*impl_;
    if(!p.time(now))return p.view.status;
    if(!p.view.attached||p.borrower!=owner||p.view.transferred){p.halt(R::Attribution);return p.view.status;}return R::Ok;
}
bool WorkExperimentBudget::complete(const void *owner,const WorkMeasurementRecord &record,const ElectricityAmount &energy,WorkMeasurementTime now){auto &p=*impl_;
    try{if(check(owner,now)!=R::Ok)return false;
        if(p.view.completed||record.input.samples.size()<2||record.receipts.empty()||!binding(record.binding,p.view.latest->initialPause.command.binding)||
            !sameWorkerCommand(record.receipts.front().command,p.view.latest->initialPause.command)||
            workProfilePlanFingerprint(record.input.plan)!=workProfilePlanFingerprint(p.plan)||record.receipts.back().kind!=WorkerReceiptKind::Paused||
            record.completed.monotonicMs!=now.monotonicMs||record.completed.utcMs!=now.utcMs||energy.numerator.negative()||
            energy.numerator.compare(p.plan.maximumEnergy.numerator)>0){p.halt(R::Attribution);return false;}
        const auto elapsed=record.input.samples.back().monotonicMs-record.input.samples.front().monotonicMs;ExactDecimal total;
        if(elapsed>p.plan.maximumElapsedMs||!p.view.observedEnergy.numerator.add(energy.numerator,&total)){p.halt(R::Capacity);return false;}
        p.view.observedElapsedMs+=elapsed;p.view.observedEnergy.numerator=std::move(total);p.view.completed=true;return true;
    }catch(...){p.halt(R::Capacity);return false;}}
bool WorkExperimentBudget::transfer(const void *owner,const WorkerReceipt &paused,WorkMeasurementTime now){auto &p=*impl_;
    if(check(owner,now)!=R::Ok)return false;
    if(!p.view.completed||paused.kind!=WorkerReceiptKind::Paused||!binding(paused.command.binding,p.view.latest->initialPause.command.binding)){p.halt(R::Attribution);return false;}
    p.handedPause=paused;p.view.transferred=true;p.transferredOwner=p.borrower;p.borrower=nullptr;return true;
}
void WorkExperimentBudget::fail(const void *owner,WorkProfileStatus status){auto &p=*impl_;
    if(p.view.attached&&(p.borrower==owner||p.transferredOwner==owner))p.halt(status);}
bool WorkExperimentBudget::retire(WorkerControlIssuer &issuer,WorkMeasurementTime now){auto &p=*impl_;
    if(!p.view.attached)return false;
    try{if(!p.time(now))return false;
        const auto s=issuer.snapshot(now.monotonicMs);
        if(!p.view.attached||!p.view.completed||!p.view.transferred||p.issuer!=&issuer||!p.handedPause||!issuer.boundTo(p.intent,p.token)||
            s.state!=WorkerControlState::Stopped||s.pending||!s.settled||!binding(s.settled->command.binding,p.handedPause->command.binding)||
            s.settled->command.revision<p.handedPause->command.revision||s.settled->observedMonoMs<p.handedPause->observedMonoMs){p.halt(R::Attribution);return false;}
        p.view.attached=false;p.issuer=p.borrower=p.transferredOwner=nullptr;p.retiredAt=now.monotonicMs;return true;
    }catch(...){p.halt(R::Capacity);return false;}}
void WorkExperimentBudget::requestStop(){impl_->halt(R::Stopped);}
const WorkExperimentBudgetSnapshot &WorkExperimentBudget::snapshot()const{return impl_->view;}
} // namespace om
