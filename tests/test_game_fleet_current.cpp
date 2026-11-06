// Owned CPU identity/held peers only. Off game detection, synthetic policy/samples; no work or device APIs.
#include "economic_fixture.h"
#include "product/game_worker_session.h"
#include "product/worker_process.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
namespace {
using namespace om;using namespace economic_fixture;
std::atomic<bool> rejectAllocations{false};std::atomic<unsigned> allocationAttempts{0};
GameGuardTime at(uint64_t ms=0){return {T+static_cast<int64_t>(ms),1000000+ms};}
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
GameEconomicPolicy economics(){GameEconomicPolicy p;p.plan=input().currentPlan;
    p.commands.enabled=p.commands.guard.enabled=p.commands.guard.margins.enabled=p.commands.guard.result.enabled=true;
    p.commands.commandLeaseMs=1000;p.commands.guard.result.maximumResultAgeMs=5000;p.commands.guard.confirmationMs=p.commands.guard.cooldownMs=0;
    p.commands.guard.minimumEvaluations=1;p.commands.guard.maximumConfirmationGapMs=5000;p.commands.guard.transitionWindowMs=10000;p.commands.guard.maximumTransitions=2;
    p.evaluation.enabled=p.evaluation.schedule.enabled=p.evaluation.result.enabled=true;p.evaluation.schedule.evaluationMs=1000;
    p.evaluation.schedule.operationTimeoutMs=500;p.evaluation.result.maximumResultAgeMs=5000;return p;
}
int child(int argc,char **argv){
    if(argc!=6||std::string(argv[2])!="--worker-control-v1")return 90;
    WorkerChannelHandles handles;if(!parseWorkerChannelHandles(argv[3],argv[4],argv[5],&handles))return 91;
    WorkerControlClient client;if(!client.adopt(handles))return 92;
    const auto end=workerControlNowMs()+10000;int code=8;bool printed=false;
    while(workerControlNowMs()<end){const auto state=client.requestState();
        if(state==WorkControlRequest::Stop){code=0;break;}
        if(state==WorkControlRequest::Fault||state==WorkControlRequest::Run)break; // this peer never accepts any work
        if(state==WorkControlRequest::Pause){client.pauseBoundary(true);client.pauseHeld();
            if(!printed){std::puts("FLEETCURRENTPAUSE");std::fflush(stdout);printed=true;}}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    client.stop();return code;
}
int live(const std::string &action,const std::filesystem::path &helper,const std::filesystem::path &exe){
    const auto original=nativeProcessEpoch();if(action=="live-foreign")fault();
    SessionIntent intent;const auto token=intent.explicitStart();auto policy=economics();
    policy.commands.commandLeaseMs=3000;policy.evaluation.schedule.operationTimeoutMs=5000;
    policy.plan.maximumElapsedMs=8000;policy.plan.maximumEnergy.numerator=d("10000000");
    GameExperimentPolicy experiment;experiment.commands={true,3000,100,5000};experiment.budget.enabled=true;
    experiment.budget.maximumReservedElapsedMs=16000;experiment.budget.maximumReservedEnergy.numerator=d("20000000");policy.experiment=experiment;
    GameWorkerPolicy workers;workers.maximumIncarnations=2;workers.commandLeaseMs=3000;workers.maximumSafetyAgeMs=4000;
    GameWorkerSession owner(helper,"fixture-unused-detector",{},{},{},{},workers,intent,token,{policy});
    CHECK(owner.start(1,gameDetectionTime()));CHECK(owner.launch(0,exe,{"--fleet-current-child"},300,gameDetectionTime()));
    uint64_t revision=0;const auto end=workerControlNowMs()+5000;
    do{const auto now=gameDetectionTime();WorkerFleetEvidence e;e.safety={token.epoch,++revision,now,{true,true,false}};owner.tick(now,e);
        if(owner.experimentSnapshot(0).initialPause)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(workerControlNowMs()<end);
    const auto before=owner.snapshot(0);const auto measured=owner.experimentSnapshot(0);const auto money=owner.economicSnapshot(0);
    CHECK(owner.state()==WorkerFleetState::Active&&before.paused&&!before.done&&!before.reaped&&!before.revoked&&measured.initialPause);
    CHECK(measured.configured&&!measured.started&&!measured.samplePending&&measured.budget.attempts==0);
    if(!measured.initialPause){owner.stop();return finishTests("held CPU peer readiness");}
    const auto now=gameDetectionTime();WorkProfileSample sample;sample.generation=token.epoch;sample.counterEpoch=1;
    sample.fingerprint=workProfileFingerprint(policy.plan.identity);sample.utcMs=now.utcMs;sample.monotonicMs=now.monotonicMs;
    CHECK(owner.submitExperimentSample(0,measured.binding,1,sample,now)==GameWorkSampleAdmission::Accepted);
    CHECK(owner.experimentSnapshot(0).samplePending);const auto schedule=owner.economicSchedule(0);CHECK(schedule.lanes[0].pending.has_value());
    if(!schedule.lanes[0].pending){owner.stop();return finishTests("held CPU external ticket readiness");}
    if(action=="live-foreign"){{NativeProcessScope old(original);
        CHECK(owner.state()==WorkerFleetState::Stopped&&!owner.snapshot(0).requested&&!owner.experimentSnapshot(0).configured);
        CHECK(!owner.setEconomicContext(0,2,true)&&!owner.requestForceResume());owner.cancelForceResume();
        CHECK(owner.submitExperimentSample(0,measured.binding,999,sample,now)==GameWorkSampleAdmission::Inactive);
        CHECK(owner.takeLines(0).empty());auto future=now;future.utcMs+=900000;future.monotonicMs+=900000;owner.tick(future,{});
        CHECK(owner.completeEconomic(0,*schedule.lanes[0].pending,OptimizationOutcome::Unreaped,future)==OptimizationCompletion::WrongTicket);}
        CHECK(owner.state()==WorkerFleetState::Active&&owner.snapshot(0).paused&&!owner.snapshot(0).revoked);
        const auto still=owner.experimentSnapshot(0);CHECK(still.samplePending&&still.initialPause&&still.budget.attempts==0);
        CHECK(owner.economicSnapshot(0).contextRevision==1&&owner.economicSchedule(0).lanes[0].pending&&!owner.snapshot(0).ownershipUnresolved);}
    if(action=="live-owner-stop"||action=="live-external")owner.requestStop();else if(action=="live-intent-stop")intent.stop();else fault();
    // First current read must cancel the sample/native channel without a tick. The actual peer still owns its exit proof.
    CHECK(owner.state()==WorkerFleetState::Stopped);const auto stopped=owner.experimentSnapshot(0);
    CHECK(!stopped.samplePending&&stopped.initialPause&&!stopped.started&&stopped.budget.attempts==0);
    CHECK(owner.snapshot(0).requested&&owner.snapshot(0).revoked&&!owner.snapshot(0).paused&&!owner.snapshot(0).retryable);
    CHECK(owner.economicSnapshot(0).evaluation.state==EconomicEvaluationState::Stopped);
    CHECK(!owner.launch(0,exe,{"--fleet-current-child"},300,now)&&!owner.setEconomicContext(0,2));
    CHECK(owner.submitExperimentSample(0,measured.binding,2,sample,now)==GameWorkSampleAdmission::Inactive);
    if(action=="live-external"){
        CHECK(owner.completeEconomic(0,*schedule.lanes[0].pending,OptimizationOutcome::Unreaped,gameDetectionTime())==OptimizationCompletion::Stopped);
        CHECK(owner.economicSchedule(0).unreaped&&owner.snapshot(0).ownershipUnresolved);}
    CHECK(before.paused&&!before.revoked&&!before.reaped&&money.evaluation.state==EconomicEvaluationState::Running);
    owner.stop();const auto final=owner.snapshot(0);CHECK(final.done&&final.reaped&&!final.retryable&&final.revoked);
    CHECK(final.ownershipUnresolved==(action=="live-external"));CHECK(!nativeProcessUnresolved());
    return finishTests("fleet current original parent/sample cancellation and actual held CPU reaping");
}
}
bool canaryNativeAllocationFault(){if(!rejectAllocations.load())return false;++allocationAttempts;return true;}
int main(int argc,char **argv){
    if(argc>=2&&std::string(argv[1])=="--fleet-current-child")return child(argc,argv);
    if(argc!=3)return 4;
    const std::string action=argv[1];
    if(action=="live-fault"||action=="live-intent-stop"||action=="live-foreign"||action=="live-owner-stop"||action=="live-external")
        return live(action,std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[0]));
    if(action!="fault"&&action!="intent-stop"&&action!="foreign"&&action!="owner-stop"&&action!="allocation"&&action!="start-foreign")return 4;
    const auto original=nativeProcessEpoch();if(action=="foreign"||action=="start-foreign")fault();
    SessionIntent intent;const auto token=intent.explicitStart();const auto helper=std::filesystem::absolute(argv[2]);
    GameWorkerPolicy policy;policy.maximumIncarnations=2;
    GameWorkerSession owner(helper,"fixture-unused-detector",{},{},{},{},policy,intent,token,{economics()});
    if(action=="start-foreign"){{NativeProcessScope old(original);CHECK(!owner.start(1,at()));CHECK(owner.state()==WorkerFleetState::Stopped);}
        CHECK(owner.state()==WorkerFleetState::Dormant);}
    CHECK(owner.start(1,at()));WorkerFleetEvidence evidence;evidence.safety={token.epoch,1,at(),{true,true,false}};
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do{owner.tick(at(1),evidence);if(owner.state()!=WorkerFleetState::Starting)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<end);
    CHECK(owner.state()==WorkerFleetState::Active&&owner.gameSnapshot().state==GameGuardState::Off);
    const auto beforeMoney=owner.economicSnapshot(0);const auto scheduled=owner.economicSchedule(0);
    CHECK(beforeMoney.configured&&beforeMoney.evaluation.state==EconomicEvaluationState::Running&&beforeMoney.budget.state==EconomicGuardState::Running&&scheduled.lanes[0].pending&&scheduled.lanes[1].pending);
    if(!scheduled.lanes[0].pending||!scheduled.lanes[1].pending){owner.stop();return finishTests("fleet economic readiness control");}
    // This executable is a CPU identity helper, never a miner, even if a broken path accidentally executes it as a worker.
    // A legitimate launch only queues here; no subsequent current tick can start a worker in this fixture.
    CHECK(owner.launch(0,helper,{"--fixture-never-launch"},300,at(1)));const auto before=owner.snapshot(0);
    CHECK(before.requested&&before.generation==1&&!before.done&&!before.reaped);
    if(action=="foreign"){{NativeProcessScope old(original);
        CHECK(owner.state()==WorkerFleetState::Stopped&&owner.gameSnapshot().state==GameGuardState::Stopped);
        const auto denied=owner.snapshot(0);CHECK(!denied.requested&&!denied.generation&&denied.revoked&&!denied.reaped&&!denied.done);
        CHECK(!owner.economicSnapshot(0).configured&&!owner.experimentSnapshot(0).configured);
        CHECK(!owner.setEconomicContext(0,2,true));CHECK(!owner.requestForceResume());owner.cancelForceResume();
        CHECK(!owner.launch(0,helper,{"--fixture-never-launch"},300,at(900000)));
        CHECK(owner.completeEconomic(0,*scheduled.lanes[0].pending,OptimizationOutcome::Complete,at(900000))!=OptimizationCompletion::Accepted);
        CHECK(owner.submitEconomic(0,*scheduled.lanes[1].pending,1,input(),at(900000))==EconomicSubmission::Inactive);
        owner.tick(at(900000),evidence);CHECK(owner.takeLines(0).empty());}
        CHECK(owner.state()==WorkerFleetState::Active&&owner.gameSnapshot().state==GameGuardState::Off);
        const auto still=owner.snapshot(0);CHECK(still.requested&&still.generation==1&&!still.done&&!still.reaped);
        const auto money=owner.economicSnapshot(0);CHECK(money.configured&&money.contextRevision==1&&money.evaluation.state==EconomicEvaluationState::Running&&money.budget.state==EconomicGuardState::Running);
        CHECK(owner.economicSchedule(0).lanes[0].pending&&owner.economicSchedule(0).lanes[1].pending);}
    if(action=="owner-stop")owner.requestStop();else if(action=="intent-stop")intent.stop();else fault();
    if(action=="allocation")rejectAllocations=true;
    bool revokedState=false,threw=false;try{revokedState=owner.state()==WorkerFleetState::Stopped&&owner.gameSnapshot().state==GameGuardState::Stopped;}catch(...){threw=true;}
    rejectAllocations=false;CHECK(revokedState&&!threw);if(action=="allocation")CHECK(allocationAttempts.load()>0); // engage the detector's existing caught projection failure
    const auto revoked=owner.snapshot(0);CHECK(revoked.requested&&revoked.generation==1&&revoked.done&&!revoked.reaped&&revoked.revoked&&!revoked.retryable&&!revoked.paused);
    CHECK(owner.economicSnapshot(0).evaluation.state==EconomicEvaluationState::Stopped&&owner.economicSnapshot(0).budget.state!=EconomicGuardState::Running);
    CHECK(!owner.forceResumeActive()&&!owner.forceResumeAvailable()&&!owner.requestForceResume());
    CHECK(before.requested&&!before.done&&!before.reaped&&beforeMoney.evaluation.state==EconomicEvaluationState::Running);
    owner.stop();CHECK(!nativeProcessUnresolved());
    return finishTests("fleet original parent/current views and stale caller (CPU identity only, no worker)");
}
