#include "economic_choice_guard.h"
#include <algorithm>
#include <deque>
namespace om {
namespace {
using S=EconomicGuardState;using A=EconomicGuardAction;using R=EconomicGuardReason;using C=EconomicChoiceState;using K=EconomicCurrentKind;
constexpr uint64_t day=86400000;constexpr int64_t lastUtc=253402300799999LL;
bool binding(const WorkerControlBinding &a,const WorkerControlBinding &b){return a.sessionKey==b.sessionKey&&a.workerKey==b.workerKey&&a.epoch==b.epoch&&a.slot==b.slot&&a.workerGeneration==b.workerGeneration;}
bool receipt(const WorkerReceipt &a,const WorkerReceipt &b){return sameWorkerCommand(a.command,b.command)&&a.kind==b.kind&&a.observedMonoMs==b.observedMonoMs;}
bool safe(const WorkProfileConditions &c){return c.safetyTelemetryValid&&c.thermalSafe&&c.gameStateKnown&&!c.gameActive&&!c.fault&&!c.drift;}
}
bool validEconomicGuardPolicy(const EconomicGuardPolicy &p){return p.margins.enabled&&validEconomicChoicePolicy(p.margins)&&p.result.enabled&&
    p.result.maximumResultAgeMs&&p.result.maximumResultAgeMs<=day&&p.confirmationMs<=3600000&&p.maximumConfirmationGapMs&&p.maximumConfirmationGapMs<=day&&
    p.cooldownMs<=day&&p.transitionWindowMs>=1000&&p.transitionWindowMs<=day&&p.minimumEvaluations&&p.minimumEvaluations<=32&&p.maximumTransitions&&p.maximumTransitions<=256;}
struct EconomicChoiceGuard::Impl {
    EconomicGuardPolicy policy;WorkerControlBinding expected;const EconomicEvaluationOwner &evaluation;WorkerControlIssuer &worker;
    const SessionIntent &intent;SessionToken token;EconomicGuardSnapshot view;bool attempted=false;
    OptimizationTime anchor,last;std::string planKey;WorkProfilePlan frozen;
    std::optional<WorkerReceipt> observed;EconomicCurrentChoice current;uint64_t modeChanged=0;bool postMode=false;std::optional<uint64_t> lastTransition;
    std::deque<uint64_t> transitions;
    std::shared_ptr<const EconomicStudy> study;std::unique_ptr<EconomicStudyUse> use;
    uint64_t resultSequence=0,sourceCeiling=0,lastEvaluation=0,blockedSequence=0;
    std::optional<size_t> confirming;uint64_t firstConfirmation=0,lastConfirmation=0,countedEvaluation=0;bool counted=false;
    Impl(EconomicGuardPolicy p,WorkerControlBinding b,const EconomicEvaluationOwner &e,WorkerControlIssuer &w,const SessionIntent &i,SessionToken t)
        :policy(std::move(p)),expected(std::move(b)),evaluation(e),worker(w),intent(i),token(t){}
    void clearChoice(){view.action=A::None;view.reason=R::Missing;view.arithmetic=C::Unavailable;view.point.reset();view.hostWaitMs.reset();view.validUntilMonoMs=0;}
    void resetConfirmation(){confirming.reset();view.confirmations=0;firstConfirmation=lastConfirmation=0;}
    void dropStudy(){study.reset();use.reset();sourceCeiling=0;view.resultSequence=0;resetConfirmation();}
    void halt(S s){view.state=s;clearChoice();view.reason=R::Terminal;dropStudy();}
    bool time(OptimizationTime now){
        if(!intent.permits(token)){halt(S::Stopped);return false;}
        if(view.state!=S::Running)return false;
        if(now.utcMs<last.utcMs||now.utcMs>lastUtc||now.monotonicMs<last.monotonicMs){halt(S::ClockFault);return false;}
        const auto wall=static_cast<uint64_t>(now.utcMs-anchor.utcMs),mono=now.monotonicMs-anchor.monotonicMs;
        if((wall>mono?wall-mono:mono-wall)>2000){halt(S::ClockFault);return false;}last=now;return true;
    }
    // Retain at most maximumTransitions originals until the NEXT application, not its possibly delayed consumption.
    void countTransitions(uint64_t now){view.transitionsInWindow=static_cast<unsigned>(std::count_if(transitions.begin(),transitions.end(),
        [&](uint64_t at){return now-at<policy.transitionWindowMs;}));}
    bool observe(OptimizationTime now){
        const auto s=worker.snapshot(now.monotonicMs);
        if(s.state==WorkerControlState::Stopped){halt(S::Stopped);return false;}
        if(s.state!=WorkerControlState::Active){halt(s.state==WorkerControlState::ClockFault?S::ClockFault:S::Faulted);return false;}
        if(s.pending){const auto &c=*s.pending;
            if(!binding(c.binding,expected)||!observed||observed->command.revision==UINT64_MAX||c.revision!=observed->command.revision+1){halt(S::Faulted);return false;}
            if(c.hostWaitMs&&std::find(frozen.hostWaitMs.begin(),frozen.hostWaitMs.end(),*c.hostWaitMs)==frozen.hostWaitMs.end()){halt(S::Faulted);return false;}
            const bool sameMode=(c.directive==WorkerDirective::Pause&&current.kind==K::Paused)||
                (c.directive==WorkerDirective::Run&&c.hostWaitMs&&current.kind==K::AppliedWait&&*c.hostWaitMs==current.hostWaitMs);
            if(!sameMode)resetConfirmation();
            view.reason=R::Pending;return false;
        }
        if(!s.settled||!binding(s.settled->command.binding,expected)||!observed){halt(S::Faulted);return false;}
        const auto &r=*s.settled;
        if(r.command.revision==observed->command.revision){if(!receipt(r,*observed)){halt(S::Faulted);return false;}}
        else{
            if(observed->command.revision==UINT64_MAX||r.command.revision!=observed->command.revision+1||r.observedMonoMs<observed->observedMonoMs){halt(S::Faulted);return false;}
            const auto previous=current;current={};
            if(r.kind==WorkerReceiptKind::Paused)current.kind=K::Paused;
            else if(r.kind==WorkerReceiptKind::WaitApplied&&r.command.hostWaitMs){current={K::AppliedWait,*r.command.hostWaitMs};
                if(std::find(frozen.hostWaitMs.begin(),frozen.hostWaitMs.end(),current.hostWaitMs)==frozen.hostWaitMs.end()){halt(S::Faulted);return false;}}
            const bool changed=previous.kind!=current.kind||(current.kind==K::AppliedWait&&previous.hostWaitMs!=current.hostWaitMs);
            if(changed){modeChanged=r.observedMonoMs;postMode=true;resetConfirmation();
                if(current.kind==K::AppliedWait){
                    while(!transitions.empty()&&r.observedMonoMs-transitions.front()>=policy.transitionWindowMs)transitions.pop_front();
                    if(transitions.size()>=policy.maximumTransitions){halt(S::Faulted);return false;}
                    transitions.push_back(r.observedMonoMs);lastTransition=r.observedMonoMs;countTransitions(now.monotonicMs);}}
            observed=r;
        }
        view.workerRevision=r.command.revision;
        if(current.kind==K::Unknown){view.action=A::Pause;view.reason=R::UnknownApplication;resetConfirmation();return false;}
        return true;
    }
    bool reference(OptimizationTime now,const WorkProfilePlan &plan,const WorkProfileConditions &conditions){
        const auto &s=evaluation.snapshot();
        if(s.study&&s.resultSequence<=blockedSequence){view.action=A::Pause;return false;}
        if(s.study&&s.resultSequence){
            if(s.resultSequence<resultSequence||(s.resultSequence==resultSequence&&study&&s.study!=study)){halt(S::Faulted);return false;}
            if(!study||s.study!=study){const auto evaluated=s.study->input().nowMonoMs;
                if(s.resultSequence==resultSequence||evaluated<lastEvaluation){halt(S::Faulted);return false;}
                study=s.study;resultSequence=s.resultSequence;lastEvaluation=evaluated;sourceCeiling=s.use.validUntilMonoMs;
                use=std::make_unique<EconomicStudyUse>(study,policy.result,intent,token);
            }else sourceCeiling=(std::min)(sourceCeiling,s.use.validUntilMonoMs);
        }else{
            if(!s.pending||s.cancellationRequested||!study||s.retainedResultSequence!=resultSequence||!s.retainedValidUntilMonoMs){dropStudy();view.action=A::Pause;return false;}
            sourceCeiling=(std::min)(sourceCeiling,s.retainedValidUntilMonoMs);
        }
        const auto &eligible=use->check(plan,now,conditions);
        if((eligible.state!=EconomicUseState::Eligible&&eligible.state!=EconomicUseState::NoPositiveCandidate)||sourceCeiling<=now.monotonicMs){
            resetConfirmation();view.action=A::Pause;return false;}
        view.resultSequence=resultSequence;view.validUntilMonoMs=(std::min)(sourceCeiling,eligible.validUntilMonoMs);
        view.validUntilMonoMs=(std::min)(view.validUntilMonoMs,observed->command.deadlineMonoMs);
        if(study->input().nowMonoMs<modeChanged||(postMode&&study->input().nowMonoMs==modeChanged)){
            view.validUntilMonoMs=0;view.reason=R::PostApplicationStudy;resetConfirmation();return false;}
        return true;
    }
    void fallback(R reason){view.reason=reason;
        if(current.kind==K::AppliedWait){view.action=A::Hold;view.hostWaitMs=current.hostWaitMs;
            view.point=static_cast<size_t>(std::find(frozen.hostWaitMs.begin(),frozen.hostWaitMs.end(),current.hostWaitMs)-frozen.hostWaitMs.begin());}
        else view.action=A::Pause;
    }
    void choose(OptimizationTime now){const auto choice=economicChoice(study.get(),policy.margins,current);view.arithmetic=choice.state;
        if(choice.state!=C::Resume&&choice.state!=C::Switch){resetConfirmation();
            if(choice.state==C::Hold){view.action=A::Hold;view.reason=R::Ready;view.point=choice.point;view.hostWaitMs=choice.hostWaitMs;}
            else{view.action=A::Pause;view.reason=choice.state==C::PauseNonpositive?R::NoPositive:choice.state==C::PauseIneligible?R::Ineligible:choice.state==C::PauseMargin?R::Margin:R::Missing;}
            return;
        }
        const auto evaluated=study->input().nowMonoMs;const bool fresh=!counted||evaluated>countedEvaluation;
        if(now.monotonicMs-evaluated>policy.maximumConfirmationGapMs){resetConfirmation();fallback(R::Confirming);return;}
        if(confirming!=choice.point||(confirming&&now.monotonicMs-lastConfirmation>policy.maximumConfirmationGapMs))resetConfirmation();
        if(fresh){if(confirming&&evaluated-lastConfirmation>policy.maximumConfirmationGapMs)resetConfirmation();
            if(!confirming){confirming=choice.point;firstConfirmation=evaluated;}
            if(view.confirmations<32)++view.confirmations;
            lastConfirmation=evaluated;countedEvaluation=evaluated;counted=true;
        }
        if(!confirming||view.confirmations<policy.minimumEvaluations||lastConfirmation-firstConfirmation<policy.confirmationMs){fallback(R::Confirming);return;}
        if(lastTransition&&now.monotonicMs-*lastTransition<policy.cooldownMs){fallback(R::Cooldown);return;}
        if(view.transitionsInWindow>=policy.maximumTransitions){fallback(R::Budget);return;}
        view.action=choice.state==C::Resume?A::Resume:A::Switch;view.reason=R::Ready;view.point=choice.point;view.hostWaitMs=choice.hostWaitMs;
        const auto confirmationEnd=policy.maximumConfirmationGapMs>=UINT64_MAX-lastConfirmation?UINT64_MAX:lastConfirmation+policy.maximumConfirmationGapMs+1;
        view.validUntilMonoMs=(std::min)(view.validUntilMonoMs,confirmationEnd);
    }
};
EconomicChoiceGuard::EconomicChoiceGuard(EconomicGuardPolicy p,WorkerControlBinding b,const EconomicEvaluationOwner &e,WorkerControlIssuer &w,const SessionIntent &i,SessionToken t)
    :impl_(new Impl(std::move(p),std::move(b),e,w,i,t)){}
EconomicChoiceGuard::~EconomicChoiceGuard()=default;
bool EconomicChoiceGuard::start(OptimizationTime now,const WorkProfilePlan &plan){auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    if(!p.intent.permits(p.token)){p.halt(S::Stopped);return false;}
    if(!p.policy.enabled){p.view.state=S::Disabled;return false;}
    try{
        if(!validEconomicGuardPolicy(p.policy)||!validWorkerControlBinding(p.expected)||p.expected.epoch!=p.token.epoch||
            !p.worker.boundTo(p.intent,p.token)||!p.evaluation.boundTo(p.intent,p.token)||validateWorkProfilePlan(plan)!=WorkProfileStatus::Ok){p.halt(S::Faulted);return false;}
        if(now.utcMs<0||now.utcMs>lastUtc||!now.monotonicMs){p.halt(S::ClockFault);return false;}
        const auto s=p.worker.snapshot(now.monotonicMs);
        if(s.state!=WorkerControlState::Active||s.pending||!s.settled||s.settled->kind!=WorkerReceiptKind::Paused||s.settled->command.revision!=1||
            !binding(s.settled->command.binding,p.expected)||p.evaluation.snapshot().state!=EconomicEvaluationState::Running){p.halt(S::Faulted);return false;}
        p.observed=s.settled;p.current.kind=K::Paused;p.modeChanged=s.settled->observedMonoMs;p.view.workerRevision=1;
        p.frozen=plan;p.planKey=workProfilePlanFingerprint(plan);p.anchor=p.last=now;p.view.state=S::Running;return true;
    }catch(...){p.halt(S::Faulted);return false;}
}
const EconomicGuardSnapshot &EconomicChoiceGuard::check(OptimizationTime now,uint64_t revision,const WorkProfilePlan &plan,const WorkProfileConditions &conditions){auto &p=*impl_;
    try{if(!p.time(now))return p.view;p.clearChoice();p.countTransitions(now.monotonicMs);
        if(!revision||revision<p.view.contextRevision||workProfilePlanFingerprint(plan)!=p.planKey){p.halt(S::Faulted);return p.view;}
        if(revision!=p.view.contextRevision){p.dropStudy();p.view.contextRevision=revision;}
        const auto &source=p.evaluation.snapshot();
        if(source.state!=EconomicEvaluationState::Running){p.halt(source.state==EconomicEvaluationState::Stopped?S::Stopped:source.state==EconomicEvaluationState::ClockFault?S::ClockFault:S::Faulted);return p.view;}
        if(source.contextRevision!=revision){p.halt(S::Faulted);return p.view;}
        if(conditions.fault){p.halt(S::Faulted);return p.view;}
        const bool unsafe=!safe(conditions);
        if(unsafe){p.blockedSequence=(std::max)(p.blockedSequence,(std::max)(source.resultSequence,p.resultSequence));
            const auto &pending=p.evaluation.schedule().lanes[1].pending;
            if(pending)p.blockedSequence=(std::max)(p.blockedSequence,pending->sequence);
            p.dropStudy();}
        if(!p.observe(now)){if(unsafe&&p.view.state==S::Running)p.view.reason=R::Unsafe;return p.view;}
        if(unsafe){p.view.action=A::Pause;p.view.reason=R::Unsafe;return p.view;}
        if(p.reference(now,plan,conditions))p.choose(now);
    }catch(...){p.halt(S::Faulted);}return p.view;
}
void EconomicChoiceGuard::requestStop(){impl_->attempted=true;impl_->halt(S::Stopped);}
const EconomicGuardSnapshot &EconomicChoiceGuard::snapshot()const{return impl_->view;}
} // namespace om
