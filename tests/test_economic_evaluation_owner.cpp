// Real CPU thread/scheduler/study integration; injected waits exist only in the separately compiled seam target.
#include "economic_fixture.h"
#include "product/economic_evaluation_owner.h"
#include "product_test_util.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
using namespace economic_fixture;
namespace {
using S=EconomicEvaluationState;using A=EconomicSubmission;using O=OptimizationOutcome;using C=OptimizationCompletion;using U=EconomicUseState;
OptimizationTime at(uint64_t ms=0){return {T+static_cast<int64_t>(ms),1000000+ms};}
EconomicEvaluationPolicy policy(){EconomicEvaluationPolicy p;p.enabled=p.schedule.enabled=p.result.enabled=true;
    p.schedule.evaluationMs=1000;p.schedule.marketRefreshMs=p.schedule.retestReviewMs=60000;p.schedule.operationTimeoutMs=500;p.schedule.retryMs=1000;p.result.maximumResultAgeMs=5000;return p;}
bool wait(const std::function<bool()> &ready){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do{if(ready())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<until);return ready();}
struct Fixture {
    SessionIntent intent;SessionToken token=intent.explicitStart();EconomicInput base=input();EconomicEvaluationOwner owner;
    uint64_t revision=1;WorkProfilePlan plan=base.currentPlan;WorkProfileConditions conditions=base.conditions;
    explicit Fixture(EconomicEvaluationPolicy p=policy()):owner(p,intent,token){CHECK(owner.start(at()));}
    OptimizationSchedulePoll poll(uint64_t ms=0){return owner.poll(at(ms),revision,plan,conditions);}
    EconomicInput fresh(const OptimizationTicket &ticket){auto in=base;in.generation=token.epoch;in.nowUtcMs=ticket.issued.utcMs;in.nowMonoMs=ticket.issued.monotonicMs;
        in.currentPlan=plan;in.conditions=conditions;reward(in);prices(in);
        in.electricity.plan=electricity_fixture::plan(in.nowUtcMs+1,in.nowUtcMs+1+86400000);in.electricity.plan.currency="USD";in.electricity.plan.revisions[0].baseRatePerKwh=d("0.5");
        in.electricity.opening=electricity_fixture::opening(in.nowUtcMs+1,in.nowUtcMs+1+86400000);in.electricity.opening.evidence=in.currentPlan.identity.power;
        for(auto &c:in.candidates)c.observedUtcMs=in.nowUtcMs;
        return in;
    }
    bool finish(uint64_t ms=10){return wait([&]{poll(ms);return !owner.snapshot().pending;});}
};
void ordinary(){
    {SessionIntent intent;auto token=intent.explicitStart();EconomicEvaluationOwner off({},intent,token);CHECK(!off.start(at())&&off.snapshot().state==S::Disabled);off.stop();CHECK(!off.start(at()));}
    for(unsigned mode=0;mode<5;++mode){SessionIntent intent;auto token=intent.explicitStart();auto p=policy();
        if(mode==0)p.schedule.enabled=false;
        if(mode==1)p.result.enabled=false;
        if(mode==2)p.result.maximumResultAgeMs=0;
        if(mode==3)p.result.maximumResultAgeMs=86400001;
        if(mode==4)p.schedule.maximumRetries=4;
        EconomicEvaluationOwner owner(p,intent,token);CHECK(!owner.start(at())&&owner.snapshot().state==S::Faulted);}
    {SessionIntent intent;auto token=intent.explicitStart();EconomicEvaluationOwner owner(policy(),intent,token);owner.requestStop();CHECK(!owner.start(at())&&owner.snapshot().state==S::Stopped);}
    {SessionIntent intent;EconomicEvaluationOwner owner(policy(),intent,{});CHECK(!owner.start(at())&&owner.snapshot().state==S::Stopped);}
    {Fixture f;auto poll=f.poll();CHECK(poll.requests[0]&&poll.requests[1]&&!poll.requests[2]);const auto ticket=*poll.requests[1];
        CHECK(f.owner.completeExternal(ticket,O::Complete,at())==C::WrongTicket&&f.owner.schedule().lanes[1].pending);
        CHECK(f.owner.submit(*poll.requests[0],1,f.fresh(ticket),at())==A::WrongTicket);
        CHECK(f.owner.completeExternal(*poll.requests[0],O::Complete,at())==C::Accepted);
        CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Accepted&&f.owner.snapshot().pending&&!f.owner.snapshot().study);
        CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Busy);
        CHECK(f.owner.completeExternal(ticket,O::Cancelled,at())==C::WrongTicket&&f.owner.schedule().lanes[1].pending);
        CHECK(f.finish());const auto result=f.owner.snapshot();CHECK(result.state==S::Running&&result.study&&result.use.state==U::Eligible&&result.use.point==0&&result.use.hostWaitMs==0);
        CHECK(result.resultSequence==ticket.sequence&&result.use.evaluated.utcMs==T&&result.use.validUntilMonoMs==at(5000).monotonicMs);
        CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at(11))==A::WrongTicket);
        CHECK(!f.poll(999).requests[1]);CHECK(f.poll(1000).requests[1].has_value());
        f.poll(5000);CHECK(!f.owner.snapshot().study&&f.owner.snapshot().use.state==U::Expired);
        f.owner.requestStop();CHECK(f.owner.snapshot().state==S::Stopped&&!f.owner.snapshot().study&&!f.owner.start(at(5001)));}
    for(unsigned mode=0;mode<8;++mode){Fixture f;const auto ticket=*f.poll().requests[1];auto changed=ticket;
        if(mode==0)++changed.generation;
        if(mode==1)++changed.sequence;
        if(mode==2)++changed.attempt;
        if(mode==3)++changed.issued.utcMs;
        if(mode==4)++changed.issued.monotonicMs;
        if(mode==5)++changed.deadlineMonoMs;
        if(mode==6)changed.lane=OptimizationLane::RetestReview;
        if(mode==7)changed.lane=static_cast<OptimizationLane>(99);
        CHECK(f.owner.submit(changed,1,f.fresh(ticket),at())==A::WrongTicket&&f.owner.schedule().lanes[1].pending&&!f.owner.snapshot().pending);}
    for(unsigned mode=0;mode<7;++mode){Fixture f;const auto ticket=*f.poll().requests[1];auto in=f.fresh(ticket);
        if(mode==0)++in.generation;
        if(mode==1)++in.nowUtcMs;
        if(mode==2)++in.nowMonoMs;
        if(mode==3)in.currentPlan.hostWaitMs[1]=11;
        if(mode==4)in.conditions.thermalSafe=false;
        if(mode==5)in.candidates.resize(33);
        if(mode==6)in.pearl->minutes.resize(1001);
        CHECK(f.owner.submit(ticket,1,std::move(in),at())==A::Invalid&&!f.owner.snapshot().pending&&f.owner.schedule().lanes[1].pending);
        CHECK(f.owner.completeExternal(ticket,O::Rejected,at())==C::Accepted);}
    {Fixture f;const auto ticket=*f.poll().requests[1];CHECK(f.owner.submit(ticket,0,f.fresh(ticket),at())==A::WrongContext);
        CHECK(f.owner.submit(ticket,2,f.fresh(ticket),at())==A::WrongContext);
        CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at(500))==A::Expired&&!f.owner.schedule().lanes[1].pending);
        CHECK(!f.poll(1499).requests[1]);const auto retry=f.poll(1500).requests[1];CHECK(retry&&retry->attempt==1);}
    for(unsigned mode=0;mode<4;++mode){Fixture f;const auto ticket=*f.poll().requests[1];auto in=f.fresh(ticket);
        if(mode==0)prices(in,"0.001");
        if(mode==1)for(auto &c:in.candidates)c.permitted=false;
        if(mode==2)in.pearl.reset();
        if(mode==3)in.pearl->observation.origin=MarketOrigin::ProviderResponse;
        CHECK(f.owner.submit(ticket,1,std::move(in),at())==A::Accepted&&f.finish());const auto &s=f.owner.snapshot();
        if(mode<2)CHECK(s.study&&s.use.state==U::NoPositiveCandidate&&!s.use.point&&!s.use.hostWaitMs);
        else CHECK(!s.study&&s.computation==(mode==2?EconomicStatus::Unavailable:EconomicStatus::Unsupported));}
    {Fixture f;const auto ticket=*f.poll().requests[1];CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Accepted&&f.finish());
        ++f.revision;f.poll(20);CHECK(!f.owner.snapshot().study&&f.owner.snapshot().contextRevision==2);
        --f.revision;f.poll(30);CHECK(f.owner.snapshot().state==S::Faulted);
        ++f.revision;f.poll(40);CHECK(f.owner.snapshot().state==S::Faulted);}
    {Fixture f;f.poll();++f.plan.hostWaitMs[1];f.poll(1);CHECK(f.owner.snapshot().state==S::Faulted);}
    for(unsigned mode=0;mode<6;++mode){Fixture f;const auto ticket=*f.poll().requests[1];CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Accepted&&f.finish());
        if(mode==0)f.conditions.thermalSafe=false;
        if(mode==1)f.conditions.safetyTelemetryValid=false;
        if(mode==2)f.conditions.gameStateKnown=false;
        if(mode==3)f.conditions.gameActive=true;
        if(mode==4)f.conditions.drift=true;
        if(mode==5)f.conditions.fault=true;
        f.poll(20);CHECK(!f.owner.snapshot().study&&!f.owner.snapshot().use.point);
        f.conditions=f.base.conditions;f.poll(30);CHECK(!f.owner.snapshot().study);
        if(mode==5)CHECK(f.owner.snapshot().state==S::Faulted);}
    for(unsigned mode=0;mode<4;++mode){Fixture f;f.poll(10);auto now=at(20);
        if(mode==0)now.utcMs=T+9;
        if(mode==1)now.monotonicMs=at(9).monotonicMs;
        if(mode==2)now.utcMs+=2001;
        if(mode==3)now.utcMs=253402300800000LL;
        f.owner.poll(now,1,f.plan,f.conditions);CHECK(f.owner.snapshot().state==S::ClockFault);f.poll(30);CHECK(f.owner.snapshot().state==S::ClockFault);}
    {Fixture f;f.poll();f.intent.stop();f.poll(1);CHECK(f.owner.snapshot().state==S::Stopped);f.intent.explicitStart();f.poll(2);CHECK(f.owner.snapshot().state==S::Stopped);}
    {Fixture f;auto p=f.poll();CHECK(f.owner.completeExternal(*p.requests[0],O::Unreaped,at(1))==C::Faulted);
        CHECK(f.owner.snapshot().state==S::Faulted&&f.owner.schedule().unreaped&&!f.owner.snapshot().study);
        f.intent.stop();f.poll(2);CHECK(f.owner.snapshot().state==S::Stopped&&f.owner.schedule().unreaped);}
    {Fixture f;const auto first=*f.poll().requests[1];CHECK(f.owner.submit(first,1,f.fresh(first),at())==A::Accepted&&f.finish());
        const auto second=*f.poll(1000).requests[1];auto in=f.fresh(second);prices(in,"0.005");
        CHECK(f.owner.submit(second,1,std::move(in),at(1000))==A::Accepted&&!f.owner.snapshot().study&&f.finish(1010));
        CHECK(f.owner.snapshot().resultSequence==second.sequence&&f.owner.snapshot().use.point==1&&f.owner.snapshot().use.hostWaitMs==10);}
    {Fixture f;const auto ticket=*f.poll().requests[1];CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Accepted&&f.finish());
        auto shifted=at(100);shifted.utcMs+=100;f.owner.poll(shifted,1,f.plan,f.conditions);
        CHECK(f.owner.snapshot().use.validUntilMonoMs==at(4900).monotonicMs);
        f.poll(300);CHECK(f.owner.snapshot().use.validUntilMonoMs==at(4900).monotonicMs);
        f.poll(4900);CHECK(!f.owner.snapshot().study&&f.owner.snapshot().use.state==U::Expired);}
    {Fixture f;auto first=f.poll();CHECK(f.owner.completeExternal(*first.requests[0],O::Complete,at())==C::Accepted);
        CHECK(f.owner.completeExternal(*first.requests[1],O::Retryable,at())==C::Accepted);
        CHECK(!f.poll(999).requests[1]);auto next=f.poll(1000);CHECK(next.requests[1]&&next.requests[1]->attempt==1&&!f.owner.snapshot().pending);
        CHECK(f.owner.completeExternal(*next.requests[1],O::Rejected,at(1000))==C::Accepted);
        auto later=f.poll(60000);CHECK(later.requests[0]&&later.requests[1]&&later.requests[2]&&!f.owner.snapshot().pending);
        CHECK(f.owner.completeExternal(*later.requests[2],O::Retryable,at(60001))==C::Accepted);
        CHECK(!f.poll(61001).requests[2]&&f.owner.schedule().lanes[2].retriesUsed==0);}
}
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
std::mutex gateMutex;std::condition_variable gateWake;bool held=false,entered=false,released=false,throwing=false,ignoreCancel=false;
unsigned calls=0;std::thread::id caller;
void hold(bool throws=false,bool ignore=false){std::lock_guard<std::mutex> lock(gateMutex);held=true;entered=released=false;throwing=throws;ignoreCancel=ignore;calls=0;caller=std::this_thread::get_id();}
bool reached(){std::unique_lock<std::mutex> lock(gateMutex);return gateWake.wait_for(lock,std::chrono::seconds(5),[]{return entered;});}
void release(){std::lock_guard<std::mutex> lock(gateMutex);released=true;gateWake.notify_all();}
void controlled(){
    // An expired CPU calculation remains owned across all later due times, even when it ignores cancellation.
    {hold(false,true);Fixture f;const auto ticket=*f.poll().requests[1];CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Accepted&&reached());
        auto out=f.poll(500);CHECK(out.cancellations[1]&&f.owner.snapshot().pending&&f.owner.snapshot().cancellationRequested&&!f.owner.snapshot().study);
        CHECK(!f.poll(10000).requests[1]&&f.owner.snapshot().pending);
        CHECK(f.owner.completeExternal(ticket,O::Cancelled,at(10000))==C::WrongTicket);
        CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at(10000))==A::Busy);
        release();CHECK(f.finish(10000)&&!f.owner.snapshot().study&&!f.owner.schedule().lanes[1].pending);
        CHECK(!f.poll(10999).requests[1]);auto next=f.poll(11000).requests[1];CHECK(next&&next->attempt==1&&calls==1);}
    for(unsigned mode=0;mode<5;++mode){hold(false,true);Fixture f;const auto ticket=*f.poll().requests[1];CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Accepted&&reached());
        if(mode==0)++f.revision;
        if(mode==1)f.conditions.gameActive=true;
        if(mode==2)f.conditions.thermalSafe=false;
        if(mode==3)f.owner.requestStop();
        if(mode==4)f.intent.stop();
        f.poll(10);CHECK(f.owner.snapshot().pending&&f.owner.snapshot().cancellationRequested&&!f.owner.snapshot().study);
        f.conditions=f.base.conditions;release();CHECK(f.finish(20)&&!f.owner.snapshot().study);
        if(mode>=3)CHECK(f.owner.snapshot().state==S::Stopped&&!f.owner.start(at(21)));
        else {const auto next=f.poll(1000).requests[1];CHECK(next&&next->attempt==0);}}
    {hold(true);Fixture f;const auto ticket=*f.poll().requests[1];CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Accepted&&reached());release();
        CHECK(f.finish()&&!f.owner.snapshot().study&&f.owner.snapshot().computation==EconomicStatus::Capacity);}
    {hold(false,true);Fixture f;const auto ticket=*f.poll().requests[1];auto in=f.fresh(ticket);
        CHECK(f.owner.submit(ticket,1,in,at())==A::Accepted&&reached());prices(in,"0.001");release();
        CHECK(f.finish()&&f.owner.snapshot().study&&f.owner.snapshot().use.state==U::Eligible);}
    {hold(false,true);auto p=policy();p.result.maximumResultAgeMs=50;Fixture f(p);const auto ticket=*f.poll().requests[1];
        CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Accepted&&reached());release();
        CHECK(f.finish(50)&&!f.owner.snapshot().study&&f.owner.snapshot().use.state==U::Expired);}
    {hold(false,true);Fixture f;const auto ticket=*f.poll().requests[1];CHECK(f.owner.submit(ticket,1,f.fresh(ticket),at())==A::Accepted&&reached());
        f.owner.requestStop();CHECK(f.owner.snapshot().pending);release();f.owner.stop();
        CHECK(!f.owner.snapshot().pending&&!f.owner.schedule().lanes[1].pending&&!f.owner.snapshot().study&&f.owner.schedule().lanes[0].pending);}
    {auto p=policy();p.schedule.evaluationMs=10000;Fixture f(p);uint64_t ms=0;
        for(unsigned attempt=0;attempt<4;++attempt){hold(false,true);const auto request=f.poll(ms).requests[1];CHECK(request&&request->attempt==attempt);
            if(!request)break;
            CHECK(f.owner.submit(*request,1,f.fresh(*request),at(ms))==A::Accepted&&reached());
            f.poll(ms+500);CHECK(f.owner.snapshot().pending&&f.owner.snapshot().cancellationRequested);release();
            CHECK(f.finish(ms+500)&&!f.owner.snapshot().study);ms+=1500;}
        CHECK(!f.poll(9999).requests[1]);const auto regular=f.poll(10000).requests[1];CHECK(regular&&regular->attempt==0);}
    {std::lock_guard<std::mutex> lock(gateMutex);held=false;}
}
#endif
}
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
namespace om {
std::shared_ptr<const EconomicStudy> economicEvaluationTestBuild(const EconomicInput &in,EconomicStatus *status,const std::atomic<bool> *cancel){
    bool fail=false,ignore=false;
    {std::unique_lock<std::mutex> lock(gateMutex);if(held){++calls;entered=true;gateWake.notify_all();
        if(std::this_thread::get_id()==caller||!gateWake.wait_for(lock,std::chrono::seconds(5),[]{return released;})){*status=EconomicStatus::Invalid;return {};}
        fail=throwing;ignore=ignoreCancel;}}
    if(fail)throw std::runtime_error("synthetic computation failure");
    return buildEconomicStudy(in,status,ignore?nullptr:cancel);
}
}
#endif
int main(){ordinary();
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
    controlled();return finishTests("economic evaluation controlled ownership");
#else
    return finishTests("economic evaluation ownership");
#endif
}
