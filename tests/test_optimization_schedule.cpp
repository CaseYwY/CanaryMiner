#include "product/optimization_schedule.h"
#include "product_test_util.h"
#include <functional>
#include <vector>
using namespace om;
namespace {
using S=OptimizationScheduleState;using O=OptimizationOutcome;using C=OptimizationCompletion;
constexpr int64_t T=1793145600000LL;
OptimizationTime at(uint64_t elapsed){return {T+static_cast<int64_t>(elapsed),1000+elapsed};}
OptimizationSchedulePolicy policy(){OptimizationSchedulePolicy p;p.enabled=true;p.evaluationMs=10000;p.marketRefreshMs=p.retestReviewMs=60000;p.retryMs=1000;p.operationTimeoutMs=500;p.maximumRetries=2;return p;}
bool empty(const OptimizationSchedulePoll &p){for(const auto &request:p.requests)if(request)return false;return true;}
}
int main(){
    SessionIntent session;const auto token=session.explicitStart();
    {OptimizationSchedule off({},session,token);CHECK(!off.start(at(0))&&off.snapshot().state==S::Disabled&&empty(off.poll(at(1))));}
    {OptimizationSchedule stopped(policy(),session,token);stopped.requestStop();CHECK(!stopped.start(at(0))&&stopped.snapshot().state==S::Stopped);}
    {OptimizationSchedule missing(policy(),session,{});CHECK(!missing.start(at(0))&&missing.snapshot().state==S::Stopped);}
    for(auto change:std::vector<std::function<void(OptimizationSchedulePolicy &)>>{
        [](auto &p){p.marketRefreshMs=59999;},[](auto &p){p.marketRefreshMs=86400001;},[](auto &p){p.evaluationMs=999;},[](auto &p){p.evaluationMs=86400001;},
        [](auto &p){p.retestReviewMs=1;},[](auto &p){p.retestReviewMs=2592000001ULL;},[](auto &p){p.retryMs=999;},[](auto &p){p.retryMs=3600001;},
        [](auto &p){p.operationTimeoutMs=499;},[](auto &p){p.operationTimeoutMs=60001;},[](auto &p){p.maximumRetries=4;}}){
        auto p=policy();change(p);OptimizationSchedule invalid(p,session,token);CHECK(!validOptimizationSchedulePolicy(p)&&!invalid.start(at(0))&&invalid.snapshot().state==S::Faulted);}
    // Initial market/evaluation requests have independent flights and schedules. Retesting starts as a later review reminder.
    {OptimizationSchedule q(policy(),session,token);CHECK(q.snapshot().state==S::Dormant&&empty(q.poll(at(0)))&&q.start(at(0))&&!q.start(at(0)));
        auto poll=q.poll(at(0));CHECK(poll.requests[0]&&poll.requests[1]&&!poll.requests[2]);const auto market=*poll.requests[0],eval=*poll.requests[1];
        CHECK(market.generation==token.epoch&&market.sequence!=eval.sequence&&market.deadlineMonoMs==1500&&market.attempt==0);
        CHECK(empty(q.poll(at(0)))&&q.complete(market,O::Complete,at(50))==C::Accepted&&q.complete(eval,O::Retryable,at(50))==C::Accepted);
        CHECK(q.snapshot().lanes[0].dueMonoMs==61000&&q.snapshot().lanes[1].dueMonoMs==2050&&empty(q.poll(at(1049))));
        poll=q.poll(at(1050));CHECK(poll.requests[1]&&poll.requests[1]->attempt==1&&!poll.requests[0]);const auto retry=*poll.requests[1];
        auto forged=retry;forged.issued.utcMs++;CHECK(q.complete(forged,O::Complete,at(1100))==C::WrongTicket&&q.snapshot().lanes[1].pending);
        CHECK(q.complete(retry,O::Retryable,at(1100))==C::Accepted);poll=q.poll(at(2100));CHECK(poll.requests[1]&&poll.requests[1]->attempt==2);
        CHECK(q.complete(*poll.requests[1],O::Retryable,at(2150))==C::Accepted&&q.snapshot().lanes[1].retriesUsed==0&&q.snapshot().lanes[1].dueMonoMs==11000);
        CHECK(empty(q.poll(at(9999))));poll=q.poll(at(10000));CHECK(poll.requests[1]&&poll.requests[1]->attempt==0);
        CHECK(q.complete(retry,O::Complete,at(10000))==C::WrongTicket&&q.complete(*poll.requests[1],O::Rejected,at(10010))==C::Accepted);
        poll=q.poll(at(60000));CHECK(poll.requests[0]&&poll.requests[1]&&poll.requests[2]);
        CHECK(q.complete(*poll.requests[2],O::Retryable,at(60001))==C::Accepted&&q.snapshot().lanes[2].dueMonoMs==121000&&q.snapshot().lanes[2].retriesUsed==0);}
    // Expiry revokes a request exactly once, but cannot manufacture reaping or free a slot for overlapping replacement.
    {OptimizationSchedule q(policy(),session,token);CHECK(q.start(at(0)));const auto first=q.poll(at(0));const auto market=*first.requests[0];
        CHECK(q.complete(*first.requests[1],O::Complete,at(1))==C::Accepted);auto p=q.poll(at(499));CHECK(!p.cancellations[0]);
        p=q.poll(at(500));CHECK(p.cancellations[0]&&p.cancellations[0]->sequence==market.sequence&&q.snapshot().lanes[0].pending&&q.snapshot().lanes[0].cancellationRequested);
        CHECK(!q.poll(at(501)).cancellations[0]);p=q.poll(at(200000));CHECK(!p.requests[0]&&q.snapshot().lanes[0].pending->sequence==market.sequence&&p.requests[1]&&p.requests[2]);
        CHECK(q.complete(market,O::Complete,at(200001))==C::Expired&&q.snapshot().lanes[0].lastExpired&&!q.snapshot().lanes[0].pending);
        p=q.poll(at(201001));CHECK(p.requests[0]&&p.requests[0]->attempt==1&&p.requests[0]->sequence!=market.sequence);
        CHECK(q.complete(market,O::Complete,at(201002))==C::WrongTicket);}
    // A completion at/after deadline is expired even if the owner has not polled to discover the timeout.
    {OptimizationSchedule q(policy(),session,token);CHECK(q.start(at(0)));auto p=q.poll(at(0));CHECK(q.complete(*p.requests[0],O::Complete,at(500))==C::Expired);
        const auto next=q.poll(at(1500));CHECK(next.requests[0]&&next.requests[0]->attempt==1);}
    // Unreaped faults all lanes, including a late timed-out flight; no new instance start on this object can restore it.
    {OptimizationSchedule q(policy(),session,token);CHECK(q.start(at(0)));auto p=q.poll(at(0));q.poll(at(500));
        CHECK(q.complete(*p.requests[0],O::Unreaped,at(501))==C::Faulted&&q.snapshot().state==S::Faulted&&q.snapshot().unreaped&&!q.start(at(600)));
        CHECK(empty(q.poll(at(700)))&&q.snapshot().lanes[1].cancellationRequested);}
    // Cancellation settles a flight without retrying it; regular scheduling has no missed-period catch-up burst.
    {auto config=policy();config.maximumRetries=0;config.retestReviewMs=0;OptimizationSchedule q(config,session,token);CHECK(q.start(at(0)));auto p=q.poll(at(0));
        CHECK(q.complete(*p.requests[0],O::Retryable,at(1))==C::Accepted&&q.complete(*p.requests[1],O::Cancelled,at(1))==C::Accepted);
        CHECK(empty(q.poll(at(9999))));p=q.poll(at(86400000));CHECK(p.requests[0]&&p.requests[1]&&!p.requests[2]);
        CHECK(q.complete(*p.requests[1],O::Complete,at(86400001))==C::Accepted&&q.snapshot().lanes[1].dueMonoMs==86411000&&empty(q.poll(at(86400002))));}
    // UTC and monotonic time are anchored together: small per-call errors cannot accumulate into a fresh-looking schedule.
    for(unsigned fault=0;fault<4;++fault){OptimizationSchedule q(policy(),session,token);CHECK(q.start(at(0)));q.poll(at(0));
        auto now=at(1);if(fault==0)now.utcMs=T-1;if(fault==1)now.monotonicMs=999;if(fault==2)now.utcMs+=2001;if(fault==3)now.monotonicMs+=2001;
        const auto p=q.poll(now);CHECK(p.state==S::ClockFault&&empty(p)&&p.cancellations[0]&&p.cancellations[1]&&!q.start(at(10)));}
    {OptimizationSchedule q(policy(),session,token);CHECK(q.start(at(0)));q.poll({T+1000,1000});q.poll({T+2000,1000});
        CHECK(q.poll({T+2001,1000}).state==S::ClockFault);}
    {auto config=policy();config.retestReviewMs=0;OptimizationSchedule q(config,session,token);OptimizationTime now{T,UINT64_MAX-100};CHECK(q.start(now));
        const auto p=q.poll(now);CHECK(p.state==S::ClockFault&&empty(p));}
    // Stop precedes clock/fault/result activity and a new explicit session cannot revive old tickets.
    {OptimizationSchedule q(policy(),session,token);CHECK(q.start(at(0)));const auto p=q.poll(at(0));session.stop();const auto stopped=q.poll(at(1));
        CHECK(stopped.state==S::Stopped&&empty(stopped)&&stopped.cancellations[0]&&stopped.cancellations[1]);
        CHECK(q.complete(*p.requests[0],O::Unreaped,at(2))==C::Stopped&&q.snapshot().unreaped&&q.snapshot().state==S::Stopped);
        session.explicitStart();CHECK(empty(q.poll(at(3)))&&!q.start(at(3))&&q.complete(*p.requests[1],O::Complete,at(4))==C::Stopped);}
    {SessionIntent fresh;const auto t=fresh.explicitStart();OptimizationSchedule q(policy(),fresh,t);CHECK(q.start(at(0)));q.poll({T-1,1000});fresh.stop();CHECK(q.poll(at(0)).state==S::Stopped);}
    return finishTests("optimization schedule");
}
