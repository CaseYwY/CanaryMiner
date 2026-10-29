#include "optimization_schedule.h"
namespace om {
namespace {
using State=OptimizationScheduleState;using C=OptimizationCompletion;using O=OptimizationOutcome;
constexpr uint64_t day=86400000;constexpr int64_t lastUtc=253402300799999LL;
bool instant(int64_t t){return t>=0&&t<=lastUtc;}
bool same(const OptimizationTicket &a,const OptimizationTicket &b){return a.generation==b.generation&&a.sequence==b.sequence&&a.lane==b.lane&&a.attempt==b.attempt&&
    a.issued.utcMs==b.issued.utcMs&&a.issued.monotonicMs==b.issued.monotonicMs&&a.deadlineMonoMs==b.deadlineMonoMs;}
}
bool validOptimizationSchedulePolicy(const OptimizationSchedulePolicy &p){
    return p.marketRefreshMs>=60000&&p.marketRefreshMs<=day&&p.evaluationMs>=1000&&p.evaluationMs<=day&&
        (!p.retestReviewMs||(p.retestReviewMs>=60000&&p.retestReviewMs<=30*day))&&p.retryMs>=1000&&p.retryMs<=3600000&&
        p.operationTimeoutMs>=500&&p.operationTimeoutMs<=60000&&p.maximumRetries<=3;
}
uint64_t OptimizationSchedule::interval(size_t lane)const{return lane==0?policy_.marketRefreshMs:lane==1?policy_.evaluationMs:policy_.retestReviewMs;}
void OptimizationSchedule::halt(State state){snapshot_.state=state;for(auto &lane:snapshot_.lanes)if(lane.pending)lane.cancellationRequested=true;}
void OptimizationSchedule::requestStop(){attempted_=true;halt(State::Stopped);}
bool OptimizationSchedule::later(uint64_t now,uint64_t delay,uint64_t &out){
    if(delay>std::numeric_limits<uint64_t>::max()-now){halt(State::ClockFault);return false;}out=now+delay;return true;
}
bool OptimizationSchedule::time(OptimizationTime now){
    if(!intent_.permits(token_)){halt(State::Stopped);return false;}
    if(snapshot_.state!=State::Running)return false;
    if(!instant(now.utcMs)||now.utcMs<last_.utcMs||now.monotonicMs<last_.monotonicMs){halt(State::ClockFault);return false;}
    const auto wall=static_cast<uint64_t>(now.utcMs-anchor_.utcMs),mono=now.monotonicMs-anchor_.monotonicMs;
    if((wall>mono?wall-mono:mono-wall)>2000){halt(State::ClockFault);return false;}
    last_=now;return true;
}
bool OptimizationSchedule::start(OptimizationTime now){
    if(attempted_)return false;
    attempted_=true;
    if(!intent_.permits(token_)){halt(State::Stopped);return false;}
    if(!policy_.enabled){snapshot_.state=State::Disabled;return false;}
    if(!validOptimizationSchedulePolicy(policy_)){halt(State::Faulted);return false;}
    if(!instant(now.utcMs)){halt(State::ClockFault);return false;}
    snapshot_.generation=token_.epoch;anchor_=last_=now;snapshot_.state=State::Running;
    for(size_t i=0;i<3;++i){auto &lane=snapshot_.lanes[i];lane.enabled=i!=2||policy_.retestReviewMs;lane.dueMonoMs=now.monotonicMs;
        if(lane.enabled&&i==2&&!later(now.monotonicMs,interval(i),lane.dueMonoMs))return false;}
    return true;
}
void OptimizationSchedule::expire(OptimizationTime now){for(auto &lane:snapshot_.lanes)if(lane.pending&&now.monotonicMs>=lane.pending->deadlineMonoMs)lane.cancellationRequested=true;}
OptimizationSchedulePoll OptimizationSchedule::poll(OptimizationTime now){
    OptimizationSchedulePoll out;
    if(attempted_&&!intent_.permits(token_))halt(State::Stopped);
    if(snapshot_.state==State::Running&&time(now)){
        expire(now);
        for(size_t i=0;i<3;++i){auto &lane=snapshot_.lanes[i];if(!lane.enabled||lane.pending||now.monotonicMs<lane.dueMonoMs)continue;
            if(sequence_==std::numeric_limits<uint64_t>::max()){halt(State::Faulted);break;}
            OptimizationTicket ticket;ticket.generation=token_.epoch;ticket.sequence=++sequence_;ticket.lane=static_cast<OptimizationLane>(i);ticket.attempt=lane.retriesUsed;ticket.issued=now;
            if(!later(now.monotonicMs,policy_.operationTimeoutMs,ticket.deadlineMonoMs))break;
            if(!lane.retriesUsed&&!later(now.monotonicMs,interval(i),regularDue_[i]))break;
            lane.pending=ticket;lane.cancellationRequested=false;cancellationDelivered_[i]=false;out.requests[i]=ticket;
        }
    }
    // A terminal transition revokes even requests prepared earlier in this poll.
    if(snapshot_.state!=State::Running)out.requests={};
    for(size_t i=0;i<3;++i){const auto &lane=snapshot_.lanes[i];if(lane.pending&&lane.cancellationRequested&&!cancellationDelivered_[i]){out.cancellations[i]=lane.pending;cancellationDelivered_[i]=true;}}
    out.state=snapshot_.state;return out;
}
void OptimizationSchedule::reschedule(size_t i,OptimizationTime now,bool retry){auto &lane=snapshot_.lanes[i];
    // A retest event is a request to review new testing, never an automatically retried physical experiment.
    if(retry&&i!=2&&lane.retriesUsed<policy_.maximumRetries){++lane.retriesUsed;later(now.monotonicMs,policy_.retryMs,lane.dueMonoMs);}
    else{lane.retriesUsed=0;if(regularDue_[i]>now.monotonicMs)lane.dueMonoMs=regularDue_[i];else later(now.monotonicMs,interval(i),lane.dueMonoMs);}
}
C OptimizationSchedule::complete(const OptimizationTicket &ticket,O outcome,OptimizationTime now){
    const auto i=static_cast<size_t>(ticket.lane);
    if(i>=3||!snapshot_.lanes[i].pending||!same(ticket,*snapshot_.lanes[i].pending))return C::WrongTicket;
    auto &lane=snapshot_.lanes[i];
    if(outcome!=O::Complete&&outcome!=O::Retryable&&outcome!=O::Rejected&&outcome!=O::Cancelled&&outcome!=O::Unreaped)return C::WrongTicket;
    if(!intent_.permits(token_))halt(State::Stopped);
    else if(snapshot_.state==State::Running)time(now);
    const bool expired=lane.cancellationRequested||(snapshot_.state==State::Running&&now.monotonicMs>=ticket.deadlineMonoMs);
    lane.lastOutcome=outcome;lane.lastExpired=expired;lane.pending.reset();lane.cancellationRequested=false;cancellationDelivered_[i]=false;
    if(outcome==O::Unreaped){snapshot_.unreaped=true;if(snapshot_.state!=State::Stopped)halt(State::Faulted);}
    if(snapshot_.state==State::Stopped)return C::Stopped;
    if(snapshot_.state!=State::Running)return C::Faulted;
    reschedule(i,now,expired||outcome==O::Retryable);
    if(snapshot_.state!=State::Running)return C::Faulted;
    return expired?C::Expired:C::Accepted;
}
} // namespace om
