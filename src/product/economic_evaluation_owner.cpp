#include "economic_evaluation_owner.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace om {
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
std::shared_ptr<const EconomicStudy> economicEvaluationTestBuild(const EconomicInput &,EconomicStatus *,const std::atomic<bool> *);
#endif
namespace {
using S=EconomicEvaluationState;using Q=OptimizationScheduleState;using O=OptimizationOutcome;using C=OptimizationCompletion;using A=EconomicSubmission;
bool same(const OptimizationTicket &a,const OptimizationTicket &b){return a.generation==b.generation&&a.sequence==b.sequence&&a.lane==b.lane&&a.attempt==b.attempt&&
    a.issued.utcMs==b.issued.utcMs&&a.issued.monotonicMs==b.issued.monotonicMs&&a.deadlineMonoMs==b.deadlineMonoMs;}
bool safe(const WorkProfileConditions &c){return c.safetyTelemetryValid&&c.thermalSafe&&c.gameStateKnown&&!c.gameActive&&!c.fault&&!c.drift;}
bool conditionsEqual(const WorkProfileConditions &a,const WorkProfileConditions &b){return a.safetyTelemetryValid==b.safetyTelemetryValid&&a.thermalSafe==b.thermalSafe&&
    a.gameStateKnown==b.gameStateKnown&&a.gameActive==b.gameActive&&a.fault==b.fault&&a.drift==b.drift;}
}
struct EconomicEvaluationOwner::Impl {
    struct Job {OptimizationTicket ticket;uint64_t revision;EconomicInput input;};
    struct Result {std::shared_ptr<const Job> job;EconomicStatus status=EconomicStatus::Unavailable;std::shared_ptr<const EconomicStudy> study;};
    EconomicEvaluationPolicy policy;const SessionIntent &intent;SessionToken token;OptimizationSchedule schedule;
    EconomicEvaluationSnapshot view;std::unique_ptr<EconomicStudyUse> use;std::shared_ptr<const Job> flight;
    bool attempted=false;OptimizationTime anchor,last;std::string planKey;WorkProfileConditions conditions;
    // Only this mailbox is shared. At most one queued/running/completed job, held until control-thread settlement.
    std::mutex mutex;std::condition_variable wake;std::thread thread;std::atomic<bool> cancel{false};
    bool stopping=false;std::shared_ptr<const Job> queued;std::optional<Result> completed;
    Impl(EconomicEvaluationPolicy p,const SessionIntent &i,SessionToken t):policy(p),intent(i),token(t),schedule(p.schedule,i,t){}
    void clearRetained(){view.retainedResultSequence=view.retainedValidUntilMonoMs=0;}
    void revoke(){if(use)use->requestStop();use.reset();view.study.reset();view.resultSequence=0;view.use={};clearRetained();}
    void cancelFlight(){clearRetained();if(flight){cancel=true;view.cancellationRequested=true;}}
    void halt(S state){
        attempted=true;view.state=state;revoke();cancelFlight();
        if(state==S::Stopped||schedule.snapshot().state==Q::Running||schedule.snapshot().state==Q::Dormant)schedule.requestStop();
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_all();
    }
    void sync(){const auto q=schedule.snapshot().state;
        if(!intent.permits(token)){halt(S::Stopped);return;}
        if(view.state!=S::Running)return;
        if(q==Q::Stopped)halt(S::Stopped);else if(q==Q::ClockFault)halt(S::ClockFault);else if(q==Q::Faulted)halt(S::Faulted);
    }
    bool time(OptimizationTime now){
        if(!intent.permits(token)){halt(S::Stopped);return false;}
        if(view.state!=S::Running)return false;
        if(now.utcMs<last.utcMs||now.utcMs>253402300799999LL||now.monotonicMs<last.monotonicMs){halt(S::ClockFault);return false;}
        const auto wall=static_cast<uint64_t>(now.utcMs-anchor.utcMs),mono=now.monotonicMs-anchor.monotonicMs;
        if((wall>mono?wall-mono:mono-wall)>2000){halt(S::ClockFault);return false;}last=now;return true;
    }
    bool pending(const OptimizationTicket &ticket)const{
        const auto lane=static_cast<size_t>(ticket.lane);const auto &s=schedule.snapshot();
        return lane<3&&s.lanes[lane].pending&&same(ticket,*s.lanes[lane].pending);
    }
    void refresh(OptimizationTime now,const WorkProfilePlan &plan){
        if(!use)return;
        view.use=use->check(plan,now,conditions);
        if(view.use.state!=EconomicUseState::Eligible&&view.use.state!=EconomicUseState::NoPositiveCandidate){view.study.reset();view.resultSequence=0;}
    }
    void run()noexcept{
        for(;;){std::shared_ptr<const Job> job;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||queued;});if(!queued)return;job=std::move(queued);}
            Result result;result.job=job;
            try{
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
                result.study=economicEvaluationTestBuild(job->input,&result.status,&cancel);
#else
                result.study=buildEconomicStudy(job->input,&result.status,&cancel);
#endif
            }catch(...){result.status=EconomicStatus::Capacity;result.study.reset();}
            {std::lock_guard<std::mutex> lock(mutex);completed=std::move(result);if(stopping)return;}
        }
    }
    void consume(OptimizationTime now,const WorkProfilePlan &plan){
        std::optional<Result> result;
        {std::lock_guard<std::mutex> lock(mutex);result=std::move(completed);completed.reset();}
        if(!result)return;
        if(!flight||result->job!=flight){halt(S::Faulted);return;}
        const auto job=flight;const bool revoked=view.state!=S::Running||cancel||job->revision!=view.contextRevision;
        auto outcome=revoked||result->status==EconomicStatus::Cancelled?O::Cancelled:result->study&&result->status==EconomicStatus::Ok?O::Complete:O::Rejected;
        const auto admission=schedule.complete(job->ticket,outcome,now);
        flight.reset();view.pending=false;view.cancellationRequested=false;view.computation=result->status;clearRetained();sync();
        if(admission==C::WrongTicket){halt(S::Faulted);return;}
        if(revoked||admission!=C::Accepted||outcome!=O::Complete||view.state!=S::Running)return;
        // The result was produced from this owned immutable job; still check its original epoch/time at handoff.
        const auto &in=result->study->input();
        if(in.generation!=token.epoch||in.nowUtcMs!=job->ticket.issued.utcMs||in.nowMonoMs!=job->ticket.issued.monotonicMs){halt(S::Faulted);return;}
        use=std::make_unique<EconomicStudyUse>(result->study,policy.result,intent,token);
        view.study=result->study;view.resultSequence=job->ticket.sequence;refresh(now,plan);
    }
};
EconomicEvaluationOwner::EconomicEvaluationOwner(EconomicEvaluationPolicy p,const SessionIntent &i,SessionToken t):impl_(new Impl(p,i,t)){}
EconomicEvaluationOwner::~EconomicEvaluationOwner(){stop();}
bool EconomicEvaluationOwner::start(OptimizationTime now){auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    if(!p.intent.permits(p.token)){p.halt(S::Stopped);return false;}
    if(!p.policy.enabled){p.view.state=S::Disabled;return false;}
    if(!p.policy.schedule.enabled||!p.policy.result.enabled||!p.policy.result.maximumResultAgeMs||p.policy.result.maximumResultAgeMs>86400000){p.halt(S::Faulted);return false;}
    if(!p.schedule.start(now)){p.view.state=p.schedule.snapshot().state==Q::ClockFault?S::ClockFault:S::Faulted;return false;}
    p.anchor=p.last=now;p.view.state=S::Running;
    try{p.thread=std::thread([&p]{p.run();});return true;}catch(...){p.halt(S::Faulted);return false;}
}
OptimizationSchedulePoll EconomicEvaluationOwner::poll(OptimizationTime now,uint64_t revision,const WorkProfilePlan &plan,const WorkProfileConditions &conditions){
    auto &p=*impl_;OptimizationSchedulePoll out;
    try{
        if(p.time(now)){
            const auto key=workProfilePlanFingerprint(plan);
            if(!revision||revision<p.view.contextRevision||key.empty()||(revision==p.view.contextRevision&&key!=p.planKey))p.halt(S::Faulted);
            else{
                if(revision!=p.view.contextRevision){p.revoke();p.cancelFlight();p.view.contextRevision=revision;p.planKey=key;}
                p.conditions=conditions;if(conditions.fault)p.halt(S::Faulted);
                else{if(!safe(conditions))p.cancelFlight();p.refresh(now,plan);}
            }
        }
        out=p.schedule.poll(now);p.sync();
        if(p.flight){const auto &lane=p.schedule.snapshot().lanes[1];
            if(!p.pending(p.flight->ticket)){p.halt(S::Faulted);}else if(lane.cancellationRequested||now.monotonicMs>=p.flight->ticket.deadlineMonoMs)p.cancelFlight();}
        p.consume(now,plan);
    }catch(...){p.halt(S::Faulted);}
    if(p.view.state!=S::Running){out.requests={};out.state=p.schedule.snapshot().state;}
    return out;
}
EconomicSubmission EconomicEvaluationOwner::submit(const OptimizationTicket &ticket,uint64_t revision,EconomicInput input,OptimizationTime now){auto &p=*impl_;
    if(!p.intent.permits(p.token)){p.halt(S::Stopped);return A::Inactive;}
    if(p.view.state!=S::Running)return A::Inactive;
    if(ticket.lane!=OptimizationLane::Evaluation||!p.pending(ticket))return A::WrongTicket;
    if(p.flight)return A::Busy;
    if(!p.time(now))return A::Inactive;
    if(now.monotonicMs>=ticket.deadlineMonoMs||p.schedule.snapshot().lanes[1].cancellationRequested){
        p.schedule.complete(ticket,O::Cancelled,now);p.revoke();p.sync();return A::Expired;}
    if(!revision||revision!=p.view.contextRevision)return A::WrongContext;
    try{
        if(input.generation!=p.token.epoch||input.nowUtcMs!=ticket.issued.utcMs||input.nowMonoMs!=ticket.issued.monotonicMs||
            workProfilePlanFingerprint(input.currentPlan)!=p.planKey||!conditionsEqual(input.conditions,p.conditions)||!safe(p.conditions)||
            input.candidates.size()>32||(input.pearl&&input.pearl->minutes.size()>1000)||(input.fx&&input.fx->minutes.size()>1000))return A::Invalid;
        auto job=std::make_shared<const Impl::Job>(Impl::Job{ticket,revision,std::move(input)});
        const auto previous=p.view.resultSequence,ceiling=p.view.use.validUntilMonoMs;
        p.revoke();p.view.retainedResultSequence=previous;p.view.retainedValidUntilMonoMs=previous?ceiling:0;
        p.cancel=false;p.flight=job;p.view.pending=true;p.view.cancellationRequested=false;p.view.computation=EconomicStatus::Unavailable;
        {std::lock_guard<std::mutex> lock(p.mutex);p.queued=std::move(job);}p.wake.notify_one();return A::Accepted;
    }catch(...){p.halt(S::Faulted);return A::Inactive;}
}
OptimizationCompletion EconomicEvaluationOwner::completeExternal(const OptimizationTicket &ticket,OptimizationOutcome outcome,OptimizationTime now){auto &p=*impl_;
    if(!p.intent.permits(p.token))p.halt(S::Stopped);
    if(ticket.lane==OptimizationLane::Evaluation&&(p.flight||outcome==O::Complete))return C::WrongTicket;
    if(!p.pending(ticket))return C::WrongTicket;
    if(p.view.state==S::Running)p.time(now);
    const auto result=p.schedule.complete(ticket,outcome,now);
    if(ticket.lane==OptimizationLane::Evaluation&&result!=C::WrongTicket)p.revoke();
    p.sync();return result;
}
void EconomicEvaluationOwner::requestStop(){impl_->halt(S::Stopped);}
void EconomicEvaluationOwner::stop(){auto &p=*impl_;requestStop();if(p.thread.joinable())p.thread.join();
    if(p.flight){p.schedule.complete(p.flight->ticket,O::Cancelled,p.last);p.flight.reset();}
    {std::lock_guard<std::mutex> lock(p.mutex);p.queued.reset();p.completed.reset();}p.view.pending=false;p.view.cancellationRequested=false;
}
const EconomicEvaluationSnapshot &EconomicEvaluationOwner::snapshot()const{return impl_->view;}
bool EconomicEvaluationOwner::boundTo(const SessionIntent &intent,SessionToken token)const{return &impl_->intent==&intent&&token.epoch&&impl_->token.epoch==token.epoch;}
const OptimizationScheduleSnapshot &EconomicEvaluationOwner::schedule()const{return impl_->schedule.snapshot();}
} // namespace om
