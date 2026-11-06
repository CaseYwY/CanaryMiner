// Current CPU study/schedule views must not outlive the explicit native parent. No helper, device, source or commands.
#include "economic_fixture.h"
#include "product/economic_evaluation_owner.h"
#include "product_test_util.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace {
using namespace om;using namespace economic_fixture;
std::atomic<bool> rejectAllocations{false};std::atomic<unsigned> allocationAttempts{0};
std::mutex gateMutex;std::condition_variable gateWake;bool held=false,entered=false,released=false;
uint64_t expectedEpoch=0;bool originalScope=false,nestedAdmission=false;unsigned calls=0;
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
bool reached(){std::unique_lock<std::mutex> lock(gateMutex);return gateWake.wait_for(lock,std::chrono::seconds(5),[]{return entered;});}
void release(){std::lock_guard<std::mutex> lock(gateMutex);released=true;gateWake.notify_all();}
}
bool canaryNativeAllocationFault(){if(!rejectAllocations.load())return false;++allocationAttempts;return true;}
namespace om {
std::shared_ptr<const EconomicStudy> economicEvaluationTestBuild(const EconomicInput &in,EconomicStatus *status,const std::atomic<bool> *cancel){
    {std::unique_lock<std::mutex> lock(gateMutex);++calls;
        if(held){entered=true;gateWake.notify_all();if(!gateWake.wait_for(lock,std::chrono::seconds(5),[]{return released;})){*status=EconomicStatus::Invalid;return {};}
            originalScope=nativeProcessAdmissionEpoch()==expectedEpoch;NativeProcessAdmission next;nestedAdmission=next.begin();if(nestedAdmission)next.settled();}}
    // Deliberately ignore cancellation only in the held path. A real late successful CPU study must still be discarded.
    return buildEconomicStudy(in,status,held?nullptr:cancel);
}
}
int main(int argc,char **argv){
    if(argc!=2)return 4;
    const std::string action=argv[1];if(action!="fault"&&action!="intent-stop"&&action!="foreign"&&action!="allocation"&&action!="scope-start"&&action!="pending-fault"&&action!="pending-stop")return 4;
    const auto original=nativeProcessEpoch();if(action=="foreign"||action=="scope-start")fault();
    SessionIntent intent;const auto token=intent.explicitStart();auto in=input();const auto plan=in.currentPlan;const auto conditions=in.conditions;
    EconomicEvaluationPolicy policy;policy.enabled=policy.schedule.enabled=policy.result.enabled=true;
    policy.schedule.evaluationMs=1000;policy.schedule.marketRefreshMs=policy.schedule.retestReviewMs=60000;
    policy.schedule.operationTimeoutMs=500;policy.schedule.retryMs=1000;policy.result.maximumResultAgeMs=5000;
    EconomicEvaluationOwner owner(policy,intent,token);const OptimizationTime now{T,1000000},later{T+10,1000010};
    if(action=="scope-start"){{NativeProcessScope old(original);CHECK(!owner.start(now));}
        CHECK(owner.snapshot().state==EconomicEvaluationState::Stopped&&!owner.start(now));owner.stop();CHECK(!nativeProcessUnresolved());return finishTests("economic stale explicit start");}
    CHECK(owner.start(now));
    const auto requests=owner.poll(now,1,plan,conditions);CHECK(requests.requests[1].has_value());if(!requests.requests[1])return finishTests("economic original parent views");
    const auto ticket=*requests.requests[1];in.generation=token.epoch;in.nowUtcMs=ticket.issued.utcMs;in.nowMonoMs=ticket.issued.monotonicMs;reward(in);prices(in);
    if(action=="foreign"){{NativeProcessScope old(original);
        CHECK(owner.submit(ticket,1,in,now)==EconomicSubmission::Inactive);
        CHECK(owner.completeExternal(*requests.requests[0],OptimizationOutcome::Complete,now)!=OptimizationCompletion::Accepted);
        const auto denied=owner.poll(now,1,plan,conditions);CHECK(denied.state!=OptimizationScheduleState::Running&&!denied.requests[0]&&!denied.requests[1]&&!denied.cancellations[0]&&!denied.cancellations[1]);}
        CHECK(owner.snapshot().state==EconomicEvaluationState::Running&&!owner.snapshot().pending);
        CHECK(owner.schedule().lanes[0].pending&&owner.schedule().lanes[1].pending&&!owner.schedule().lanes[0].cancellationRequested&&!owner.schedule().lanes[1].cancellationRequested);}
    if(action=="pending-fault"||action=="pending-stop"){held=true;expectedEpoch=nativeProcessEpoch();}
    CHECK(owner.submit(ticket,1,std::move(in),now)==EconomicSubmission::Accepted);
    if(held){CHECK(reached());if(action=="pending-stop")intent.stop();else fault();
        const auto &revoked=owner.snapshot();CHECK(revoked.state==EconomicEvaluationState::Stopped&&revoked.pending&&revoked.cancellationRequested&&!revoked.study&&!revoked.use.point);
        const auto &schedule=owner.schedule();CHECK(schedule.state==OptimizationScheduleState::Stopped&&schedule.lanes[0].pending&&schedule.lanes[1].pending&&schedule.lanes[0].cancellationRequested&&schedule.lanes[1].cancellationRequested);
        CHECK(owner.completeExternal(ticket,OptimizationOutcome::Cancelled,later)==OptimizationCompletion::WrongTicket&&owner.snapshot().pending);
        release();owner.stop();CHECK(originalScope);CHECK(nestedAdmission==(action=="pending-stop"));CHECK(calls==1);
        CHECK(!owner.snapshot().pending&&!owner.snapshot().study&&!owner.schedule().lanes[1].pending&&owner.schedule().lanes[0].pending);
        CHECK(owner.completeExternal(*requests.requests[0],OptimizationOutcome::Unreaped,later)==OptimizationCompletion::Stopped);
        CHECK(owner.schedule().unreaped&&!owner.schedule().lanes[0].pending&&!nativeProcessUnresolved());return finishTests("economic real held CPU ownership/native scope");}
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do{owner.poll(later,1,plan,conditions);if(!owner.snapshot().pending)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<end);
    const auto before=owner.snapshot();CHECK(before.state==EconomicEvaluationState::Running&&before.study&&before.use.state==EconomicUseState::Eligible&&before.use.point.has_value());
    if(action=="foreign"){{NativeProcessScope old(original);CHECK(!owner.snapshot().study&&!owner.snapshot().use.point);CHECK(owner.schedule().state!=OptimizationScheduleState::Running);}CHECK(owner.snapshot().study==before.study&&owner.snapshot().state==EconomicEvaluationState::Running);}
    if(action=="intent-stop")intent.stop();else fault();
    if(action=="allocation")rejectAllocations=true;
    const bool withdrawn=owner.snapshot().state==EconomicEvaluationState::Stopped&&!owner.snapshot().study&&!owner.snapshot().use.point;
    const bool stopped=owner.schedule().state!=OptimizationScheduleState::Running;
    rejectAllocations=false;CHECK(withdrawn);CHECK(stopped);if(action=="allocation")CHECK(allocationAttempts.load()==0);
    CHECK(before.study&&before.use.point.has_value());owner.stop();CHECK(!nativeProcessUnresolved());
    return finishTests("economic current views versus original native/explicit parent (real CPU study only)");
}
