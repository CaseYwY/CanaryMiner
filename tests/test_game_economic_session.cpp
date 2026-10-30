// Self-owned CPU peers over the real native channel; all financial/work inputs are explicitly synthetic.
#include "economic_fixture.h"
#include "product/game_worker_session.h"
#include "product/worker_process.h"
#include "product_test_util.h"
#include <thread>
using namespace economic_fixture;
namespace {
using F=WorkerFleetState;using C=EconomicCommandState;using D=WorkerDirective;using K=WorkerReceiptKind;
std::filesystem::path identity,detector,exe;
void sleep(unsigned ms=5){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
int child(int argc,char **argv){
    if(argc!=7||std::string(argv[3])!="--worker-control-v1")return 90;
    WorkerChannelHandles handles;if(!parseWorkerChannelHandles(argv[4],argv[5],argv[6],&handles))return 91;
    WorkerControlClient client;if(!client.adopt(handles))return 92;
    const std::string mode=argv[2];const auto began=workerControlNowMs();uint64_t first=0,printed=0;unsigned installed=999;int code=8;bool measured=false;
    while(workerControlNowMs()-began<15000){const auto state=client.requestState();
        if(state==WorkControlRequest::Stop){code=0;break;}
        if(state==WorkControlRequest::Fault)break;
        if(state==WorkControlRequest::Pause){client.pauseBoundary(true);client.pauseHeld();}
        else if(state==WorkControlRequest::Run){client.pauseBoundary(false);
            if(mode!="withheld"&&mode!="exit-pending")client.applyWorkWait(installed);
        }
        const auto view=client.snapshot();
        if(view.receipt&&view.receipt->kind==K::Paused&&view.receipt->command.revision>1)measured=true;
        if(view.receipt&&view.receipt->command.revision!=printed){printed=view.receipt->command.revision;
            if(view.receipt->command.directive==D::Run){
                std::puts(view.receipt->kind==K::WaitApplied?"ECONAPPLIED":"ECONACCEPTED");std::fflush(stdout);
                if(!first&&(mode!="measured-restart"||measured))first=workerControlNowMs();
            }
        }
        if(first&&workerControlNowMs()-first>=200){
            if(mode=="exit-pending"||mode=="exit-error"||((mode=="restart-once"||mode=="measured-restart")&&view.receipt&&view.receipt->command.binding.workerGeneration==1)){code=1;break;}
            if(mode=="exit-clean"){code=0;break;}
            if(mode=="exit-fault"){code=8;break;}
        }
        sleep();
    }
    client.stop();return code;
}
GameEconomicPolicy economicPolicy(){GameEconomicPolicy p;p.plan=input().currentPlan;
    auto &c=p.commands;c.enabled=c.guard.enabled=c.guard.margins.enabled=c.guard.result.enabled=true;c.commandLeaseMs=3000;
    c.guard.result.maximumResultAgeMs=5000;c.guard.confirmationMs=0;c.guard.minimumEvaluations=1;c.guard.maximumConfirmationGapMs=5000;
    c.guard.cooldownMs=0;c.guard.transitionWindowMs=10000;c.guard.maximumTransitions=2;
    auto &e=p.evaluation;e.enabled=e.schedule.enabled=e.result.enabled=true;e.schedule.evaluationMs=1000;e.schedule.operationTimeoutMs=500;e.schedule.retryMs=1000;
    e.result.maximumResultAgeMs=5000;return p;
}
struct Rig {
    SessionIntent intent;SessionToken token=intent.explicitStart();uint64_t anchor=workerControlNowMs(),safetyRevision=0;
    unsigned count;GameWorkerPolicy workers;std::vector<GameEconomicPolicy> policies;std::unique_ptr<GameWorkerSession> session;
    std::vector<std::string> modes,lines;
    explicit Rig(unsigned n=1,unsigned transitions=2):count(n),policies(n,economicPolicy()),modes(n,"normal"),lines(n){
        workers.maximumIncarnations=4;workers.commandLeaseMs=3000;workers.maximumSafetyAgeMs=4000;
        for(auto &p:policies)p.commands.guard.maximumTransitions=transitions;
        reset();
    }
    void reset(){GameGuardPolicy game;game.mode=GameIntent::Off;
        session=std::make_unique<GameWorkerSession>(identity,detector,GameProcessTarget{},GameDetectionPolicy{},game,GameProfitBinding{},workers,intent,token,policies);}
    GameGuardTime now()const{const auto t=workerControlNowMs();return {T+static_cast<int64_t>(t-anchor),t};}
    WorkerFleetEvidence fresh(GameGuardTime t){WorkerFleetEvidence e;e.safety={token.epoch,++safetyRevision,t,{true,true,false}};return e;}
    bool launch(unsigned i){return session->launch(i,exe,{"--economic-child",modes[i]},300,now());}
    bool start(const std::string &mode="normal"){modes[0]=mode;if(!session->start(count,now()))return false;
        for(unsigned i=0;i<count;++i)if(!launch(i))return false;
        return true;
    }
    void step(const WorkerFleetEvidence *frozen=nullptr){const auto t=now();session->tick(t,frozen?*frozen:fresh(t));
        for(unsigned i=0;i<count;++i)for(const auto &line:session->takeLines(i))lines[i]+=line+'\n';}
    bool until(const std::function<bool()> &done,unsigned ms=5000,const WorkerFleetEvidence *frozen=nullptr){const auto end=workerControlNowMs()+ms;
        do{step(frozen);if(done())return true;if(session->state()==F::Faulted||session->state()==F::Stopped)return false;sleep();}while(workerControlNowMs()<end);return false;}
    GameEconomicSnapshot economy(unsigned i=0)const{return session->economicSnapshot(i);}
    WorkerFleetSnapshot view(unsigned i=0)const{return session->snapshot(i);}
    bool ready(){return until([&]{for(unsigned i=0;i<count;++i)if(!economy(i).commandStarted||!view(i).paused)return false;return true;});}
    // Startup's original ticket can precede the initial Paused receipt. Explicitly settle it; never restamp it.
    void discard(unsigned i=0){const auto ticket=session->economicSchedule(i).lanes[1].pending;if(!ticket)return;
        const auto result=session->completeEconomic(i,*ticket,OptimizationOutcome::Rejected,now());
        CHECK(result==OptimizationCompletion::Accepted||result==OptimizationCompletion::Expired);
    }
    std::optional<OptimizationTicket> ticket(unsigned i=0){std::optional<OptimizationTicket> result;
        CHECK(until([&]{result=session->economicSchedule(i).lanes[1].pending;return result.has_value();}));return result;}
    EconomicInput original(const OptimizationTicket &t,unsigned i=0,const char *price="0.01")const{
        const auto experiment=session->experimentSnapshot(i);
        auto in=experiment.commands.record?input(buildWorkProfile(experiment.commands.record->measurement->input)):input();
        in.generation=token.epoch;in.nowUtcMs=t.issued.utcMs;in.nowMonoMs=t.issued.monotonicMs;
        in.conditions=economy(i).conditions;prices(in,price);
        in.electricity.opening.throughUtcMs=t.issued.utcMs+1;
        in.electricity.opening.timeDatabaseVersion=civilAt(in.electricity.plan.clock,t.issued.utcMs+1).databaseVersion;return in;
    }
    bool study(unsigned i=0,const char *price="0.01"){
        const auto t=ticket(i);if(!t)return false;
        CHECK(session->submitEconomic(i,*t,economy(i).contextRevision,original(*t,i,price),now())==EconomicSubmission::Accepted);
        return until([&]{const auto s=economy(i);return !s.evaluation.pending&&s.evaluation.resultSequence==t->sequence;});
    }
    bool running(unsigned i=0){return until([&]{return economy(i).commands.application.has_value()&&!economy(i).commands.pending;});}
    bool begin(const std::string &mode="normal"){if(!start(mode)||!ready())return false;
        for(unsigned i=0;i<count;++i)discard(i);
        for(unsigned i=0;i<count;++i)if(!study(i))return false;
        return true;
    }
    void measurements(unsigned attempts=2){for(auto &p:policies){auto &plan=p.plan;plan.telemetryCadenceMs=10;plan.maximumSampleGapMs=1000;
        plan.warmupMs=20;plan.measurementMs=50;plan.repetitions=1;plan.maximumElapsedMs=8000;plan.maximumEnergy.numerator=d("10000000");
        GameExperimentPolicy e;e.commands={true,workers.commandLeaseMs,100,5000};e.budget.enabled=true;e.budget.maximumAttempts=attempts;
        e.budget.minimumRetestMs=0;e.budget.maximumReservedElapsedMs=16000;e.budget.maximumReservedEnergy.numerator=d("20000000");p.experiment=e;}reset();}
    GameExperimentSnapshot experiment(unsigned i=0)const{return session->experimentSnapshot(i);}
    bool baselineReady(){return until([&]{for(unsigned i=0;i<count;++i)if(!experiment(i).initialPause)return false;return true;});}
    WorkProfileSample sample(GameGuardTime t,unsigned i=0)const{const auto s=experiment(i);WorkProfileSample out;
        out.generation=token.epoch;out.counterEpoch=s.workerGeneration;out.fingerprint=workProfileFingerprint(policies[i].plan.identity);
        out.utcMs=t.utcMs;out.monotonicMs=t.monotonicMs;
        out.appliedHostWaitMs=s.measurement.latestSample?s.measurement.latestSample->appliedHostWaitMs:0;
        if(s.commands.receipt&&s.commands.receipt->kind==K::WaitApplied)out.appliedHostWaitMs=*s.commands.receipt->command.hostWaitMs;
        out.completeWorkBoundary=out.safetyTelemetryValid=out.thermalSafe=out.gameStateKnown=true;
        out.milliwatts=out.appliedHostWaitMs?100000:200000;
        out.userTmacs=d(std::to_string((t.monotonicMs-anchor)*2));out.donationTmacs=d(std::to_string(t.monotonicMs-anchor));return out;}
    bool feed(unsigned i=0){const auto s=experiment(i);const auto t=now();
        if(economy(i).commandStarted||s.commands.pending||s.samplePending||!s.initialPause||
            (s.measurement.latestSample&&t.monotonicMs-s.measurement.latestSample->monotonicMs<policies[i].plan.telemetryCadenceMs))return true;
        return session->submitExperimentSample(i,s.binding,s.measurement.sequence+1,sample(t,i),t)==GameWorkSampleAdmission::Accepted;}
    bool measure(){const auto end=workerControlNowMs()+5000;while(workerControlNowMs()<end){step();bool done=true;
        if(session->state()!=F::Active)return false;
        for(unsigned i=0;i<count;++i){done=done&&economy(i).commandStarted;if(!feed(i))return false;}
        if(done)return true;
        sleep();}return false;}
};
void nativeCommands(){Rig r;CHECK(r.start()&&r.ready());
    CHECK(r.view().paused&&r.view().commandRevision==1&&!r.economy().commands.lastRun);
    CHECK(r.lines[0].find("ECONAPPLIED")==std::string::npos);r.discard();CHECK(r.study()&&r.running());
    const auto first=r.economy();const auto original=first.commands.lastRun;const auto applied=*first.commands.application;
    CHECK(original&&original->command.hostWaitMs==0&&sameWorkerCommand(applied.command,original->command)&&applied.kind==K::WaitApplied);
    CHECK(applied.observedMonoMs>=original->command.issuedMonoMs&&applied.observedMonoMs<original->command.deadlineMonoMs&&first.budget.transitionsInWindow==1);
    CHECK(!r.view().pauseRequested&&!r.view().paused&&!r.view().commandPending&&original->study==first.evaluation.study);
    CHECK(r.study()&&r.running());const auto renewed=r.economy();
    CHECK(renewed.commands.lastRun->command.revision>original->command.revision&&renewed.commands.lastRun->command.hostWaitMs==0&&
        renewed.commands.lastRun->decision.resultSequence!=original->decision.resultSequence&&renewed.budget.transitionsInWindow==1);
    CHECK(original->command.revision==applied.command.revision&&sameWorkerCommand(original->command,applied.command));
    CHECK(r.session->setEconomicContext(0,2));r.step();CHECK(r.until([&]{return r.view().paused;}));
    CHECK(!r.economy().evaluation.study&&r.economy().commands.lastRun==renewed.commands.lastRun);
    CHECK(r.study(0,"0.005")&&r.running());CHECK(r.economy().commands.lastRun->command.hostWaitMs==10&&r.economy().budget.transitionsInWindow==2);
    auto unsafe=r.fresh(r.now());unsafe.safety.safety.thermalSafe=false;r.step(&unsafe);
    CHECK(r.until([&]{return r.view().paused;},1000,&unsafe));CHECK(!r.economy().evaluation.study&&r.session->state()==F::Active);
    r.session->requestStop();CHECK(r.view().revoked&&r.economy().commands.revokeRequired);r.session->stop();
    CHECK(r.view().reaped&&r.view().done&&r.view().exitCode==0&&!r.view().retryable);
    r.intent.stop();r.intent.explicitStart();r.step();CHECK(r.session->state()==F::Stopped&&!r.launch(0));
}
void pendingWithdrawal(){
    for(unsigned mode=0;mode<6;++mode){Rig r(2);CHECK(r.begin("withheld"));CHECK(r.running(1));
        CHECK(r.until([&]{return r.lines[0].find("ECONACCEPTED")!=std::string::npos;}));const auto pending=r.economy();
        CHECK(pending.commands.pending&&r.view().commandPending&&!pending.commands.application&&pending.budget.transitionsInWindow==0&&pending.commands.lastRun);
        const auto deadline=pending.commands.lastRun->command.deadlineMonoMs;
        if(mode==0){CHECK(r.session->setEconomicContext(0,2));r.step();}
        if(mode==1){CHECK(r.session->setEconomicContext(0,1,true));r.step();}
        if(mode==2){auto unsafe=r.fresh(r.now());unsafe.safety.safety.telemetryValid=false;r.step(&unsafe);}
        if(mode==3){auto proof=r.fresh(r.now());r.step(&proof);auto skewed=r.now();skewed.utcMs+=1500;r.session->tick(skewed,proof);}
        if(mode==4){r.intent.stop();r.step();}
        if(mode==5)r.session->requestStop();
        CHECK(r.session->state()==(mode>=4?F::Stopped:F::Faulted)&&workerControlNowMs()<deadline);
        for(unsigned i=0;i<2;++i)CHECK(r.view(i).revoked&&r.view(i).pauseRequested&&!r.view(i).paused&&r.economy(i).commands.revokeRequired);
        r.step();CHECK(!r.launch(0)&&!r.session->setEconomicContext(0,3));r.session->stop();
        for(unsigned i=0;i<2;++i)CHECK(r.view(i).done&&r.view(i).reaped&&!r.view(i).retryable&&r.view(i).exitCode==0);
    }
}
void replacement(){
    {Rig r;CHECK(r.begin("restart-once")&&r.running());const auto before=r.economy();const auto binding=before.commands.lastRun->command.binding;
        CHECK(!r.launch(0));
        CHECK(r.until([&]{return r.view().done;}));const auto retired=r.economy();
        CHECK(r.view().reaped&&r.view().retryable&&r.view().exitCode==1&&retired.commands.state==C::Retired&&retired.budget.transitionsInWindow==1&&!retired.budget.attached);
        CHECK(r.launch(0));CHECK(!r.economy().commandStarted&&!r.economy().commands.lastRun);
        CHECK(r.ready()&&r.view().generation==2&&!r.economy().commands.lastRun);
        CHECK(r.economy().evaluation.study==retired.evaluation.study&&r.economy().budget.incarnations==2&&r.economy().budget.transitionsInWindow==1);
        CHECK(r.study()&&r.running());const auto next=r.economy();
        CHECK(next.commands.lastRun->command.binding.sessionKey==binding.sessionKey&&next.commands.lastRun->command.binding.workerKey!=binding.workerKey&&
            next.commands.lastRun->command.binding.workerGeneration==2&&next.budget.transitionsInWindow==2);
        r.session->stop();CHECK(r.view().done&&r.view().reaped&&r.view().exitCode==0);
    }
    {Rig r(1,1);CHECK(r.begin("restart-once")&&r.running());CHECK(r.until([&]{return r.view().done;}));CHECK(r.view().retryable&&r.launch(0)&&r.ready());
        CHECK(r.study());CHECK(r.view().paused&&!r.economy().commands.lastRun&&r.economy().budget.transitionsInWindow==1);}
    {Rig r;r.policies[0].commands.guard.cooldownMs=2500;r.reset();CHECK(r.begin("restart-once")&&r.running());
        CHECK(r.until([&]{return r.view().done;}));CHECK(r.view().retryable&&r.launch(0)&&r.ready());
        CHECK(r.study());CHECK(r.view().paused&&!r.economy().commands.lastRun&&r.economy().budget.transitionsInWindow==1);
        CHECK(r.study());CHECK(r.view().paused&&!r.economy().commands.lastRun);
        CHECK(r.study()&&r.running());CHECK(r.economy().budget.transitionsInWindow==2);}
    {Rig r;CHECK(r.begin("exit-error"));
        // Deliberately do not consume the native owner's application receipt before exit. Unknown history cannot retry.
        const auto end=workerControlNowMs()+2000;while(!r.view().reaped&&workerControlNowMs()<end)sleep();
        r.step();CHECK(r.view().done&&r.view().reaped&&!r.view().retryable&&!r.launch(0));}
    for(const auto *mode:{"exit-clean","exit-fault","exit-pending"}){Rig r;CHECK(r.begin(mode));CHECK(r.until([&]{return r.view().done;}));
        CHECK(r.view().reaped&&r.view().revoked&&!r.view().retryable&&!r.launch(0));
        CHECK(r.view().exitCode==(std::string(mode)=="exit-clean"?0:std::string(mode)=="exit-fault"?8:1));}
}
void admissionAndTickets(){
    for(unsigned mode=0;mode<5;++mode){Rig r;
        if(mode==0)r.policies.push_back(economicPolicy());
        if(mode==1)r.policies[0].commands.enabled=false;
        if(mode==2)r.policies[0].commands.commandLeaseMs=1000;
        if(mode==3)r.policies[0].plan.qualification=WorkProfileQualification::Unsupported;
        if(mode==4)r.policies[0].evaluation.schedule.operationTimeoutMs=499;
        r.reset();CHECK(!r.start()&&r.session->state()==F::Faulted&&!r.view().requested);r.session->stop();}
    {Rig r;r.session->requestStop();CHECK(!r.start()&&!r.session->setEconomicContext(0,1));}
    {Rig r;CHECK(r.begin()&&r.running());const auto market=*r.session->economicSchedule(0).lanes[0].pending;
        CHECK(r.session->completeEconomic(0,market,OptimizationOutcome::Unreaped,r.now())==OptimizationCompletion::Faulted);
        CHECK_MSG("known unresolved external ownership revokes the native channel before the callback returns",
            r.session->state()==F::Faulted&&r.economy().commands.revokeRequired&&r.view().ownershipUnresolved);
        r.session->stop();CHECK(r.view().done&&r.view().reaped&&!r.view().retryable);}
    {Rig r;CHECK(r.start()&&r.ready());const auto t=*r.session->economicSchedule(0).lanes[1].pending;
        CHECK(r.session->completeEconomic(0,t,OptimizationOutcome::Complete,r.now())==OptimizationCompletion::WrongTicket);
        auto forged=t;++forged.sequence;CHECK(r.session->submitEconomic(0,forged,1,r.original(t),r.now())==EconomicSubmission::WrongTicket);
        CHECK(r.session->submitEconomic(0,t,2,r.original(t),r.now())==EconomicSubmission::WrongContext);
        auto wrong=r.original(t);++wrong.nowMonoMs;CHECK(r.session->submitEconomic(0,t,1,std::move(wrong),r.now())==EconomicSubmission::Invalid);
        CHECK(r.until([&]{return r.session->economicSchedule(0).lanes[1].cancellationRequested;}));
        CHECK(r.session->submitEconomic(0,t,1,r.original(t),r.now())==EconomicSubmission::Expired);
        CHECK(!r.session->economicSchedule(0).lanes[1].pending&&!r.economy().commands.lastRun&&r.view().paused);
        CHECK(r.study()&&r.running());const auto market=*r.session->economicSchedule(0).lanes[0].pending;
        r.session->stop();CHECK(r.session->completeEconomic(0,market,OptimizationOutcome::Unreaped,r.now())==OptimizationCompletion::Stopped);
        CHECK(r.view().ownershipUnresolved&&r.view().exitCode==8&&!r.launch(0));
    }
}
void measuredSession(){using A=GameWorkSampleAdmission;Rig r;r.measurements();CHECK(r.start()&&r.baselineReady());
    CHECK(!r.economy().commandStarted&&!r.experiment().started&&r.experiment().budget.attempts==0&&r.view().commandRevision==1);
    const auto t=r.now();const auto original=r.sample(t);const auto binding=r.experiment().binding;
    auto wrongGeneration=binding;++wrongGeneration.workerGeneration;
    CHECK(r.session->submitExperimentSample(0,wrongGeneration,1,original,t)==A::WrongGeneration&&r.session->state()==F::Active);
    for(unsigned part=0;part<4;++part){auto foreign=binding;
        if(part==0)++foreign.epoch;
        if(part==1)++foreign.slot;
        if(part==2)foreign.sessionKey=std::string(32,'f');
        if(part==3)foreign.workerKey=std::string(32,'f');
        CHECK(r.session->submitExperimentSample(0,foreign,1,original,t)==A::WrongBinding&&!r.experiment().samplePending&&r.session->state()==F::Active);}
    CHECK(r.session->submitExperimentSample(0,binding,1,original,t)==A::Accepted&&r.experiment().samplePending);
    CHECK(r.session->submitExperimentSample(0,binding,1,original,t)==A::Full);
    sleep();r.step();CHECK(r.experiment().started&&r.experiment().budget.attempts==1&&r.experiment().budget.attached);
    CHECK(r.experiment().measurement.latestSample&&r.experiment().measurement.latestSample->monotonicMs==original.monotonicMs);
    const auto first=r.experiment().budget.latest;const auto initialTicket=r.session->economicSchedule(0).lanes[1].pending;
    if(initialTicket)CHECK(r.session->submitEconomic(0,*initialTicket,1,r.original(*initialTicket),r.now())==EconomicSubmission::Inactive);
    CHECK(r.measure());const auto snapshot=r.experiment();const auto record=snapshot.commands.record;
    CHECK(record&&snapshot.commands.state==WorkExperimentState::Transferred&&snapshot.budget.completed&&snapshot.budget.transferred&&snapshot.budget.attached);
    CHECK(record&&record==r.economy().commands.measurement&&record->measurement->binding.workerKey==binding.workerKey&&
        record->measurement->input.windows.size()==2&&record->measurement->receipts.front().kind==K::Paused&&record->measurement->receipts.back().kind==K::Paused);
    CHECK(r.view().paused&&!r.economy().commands.lastRun&&r.economy().budget.attached&&r.economy().budget.transitionsInWindow==0);
    CHECK(r.session->submitExperimentSample(0,binding,999,r.sample(r.now()),r.now())==A::Inactive);
    r.discard();const auto ticket=r.ticket();CHECK(ticket.has_value());if(ticket){auto wrong=r.original(*ticket);auto altered=wrong.profile->input();
        for(auto &s:altered.samples){ExactDecimal sum;s.userTmacs->add(d("1"),&sum);s.userTmacs=sum;}
        wrong.profile=buildWorkProfile(altered);CHECK(wrong.profile&&workProfilePlanFingerprint(wrong.currentPlan)==workProfilePlanFingerprint(altered.plan));
        CHECK(r.session->submitEconomic(0,*ticket,1,std::move(wrong),r.now())==EconomicSubmission::Invalid&&!r.economy().evaluation.pending);
        CHECK(r.session->submitEconomic(0,*ticket,1,r.original(*ticket),r.now())==EconomicSubmission::Accepted);
        CHECK(r.until([&]{return !r.economy().evaluation.pending&&r.economy().evaluation.resultSequence==ticket->sequence;}));}
    CHECK(r.running()&&r.economy().budget.transitionsInWindow==1&&r.experiment().budget.latest==first);
    CHECK(record&&sameWorkProfileInput(record->measurement->input,r.economy().commands.lastRun->study->input().profile->input()));
    r.session->stop();CHECK(r.view().done&&r.view().reaped&&!r.view().retryable&&r.experiment().budget.attached&&r.experiment().budget.attempts==1);
    CHECK(record==r.experiment().commands.record&&first->elapsedMs==r.policies[0].plan.maximumElapsedMs&&!r.launch(0));
}
void measuredReplacement(){using A=GameWorkSampleAdmission;
    for(unsigned attempts:{1u,2u}){Rig r;r.measurements(attempts);CHECK(r.start("measured-restart")&&r.baselineReady()&&r.measure());
        const auto before=r.experiment();r.discard();CHECK(r.study()&&r.running()&&r.until([&]{return r.view().done;}));
        CHECK(r.view().retryable&&r.view().reaped&&r.view().exitCode==1&&!r.experiment().budget.attached&&!r.economy().budget.attached);
        const auto priorProfile=buildWorkProfile(before.commands.record->measurement->input);
        CHECK(r.launch(0)&&!r.experiment().initialPause&&!r.experiment().commands.record&&r.experiment().budget.attempts==1);
        CHECK(r.baselineReady()&&r.experiment().binding.workerGeneration==2&&r.experiment().binding.workerKey!=before.binding.workerKey);
        CHECK(r.session->submitExperimentSample(0,before.binding,1,r.sample(r.now()),r.now())==A::WrongGeneration&&r.session->state()==F::Active);
        if(attempts==1){CHECK(r.feed());r.step();CHECK(r.session->state()==F::Faulted&&r.experiment().budget.attempts==1&&
            r.experiment().budget.reservedElapsedMs==8000&&!r.economy().commandStarted);}
        else{CHECK(r.measure()&&r.experiment().budget.attempts==2&&r.experiment().budget.reservedElapsedMs==16000&&r.economy().budget.incarnations==2);
            const auto ticket=r.ticket();if(ticket){auto wrong=r.original(*ticket);wrong.profile=priorProfile;
                CHECK(r.session->submitEconomic(0,*ticket,1,std::move(wrong),r.now())==EconomicSubmission::Invalid);}
            CHECK(r.study()&&r.running()&&r.economy().budget.transitionsInWindow==2);
            CHECK(before.commands.record!=r.experiment().commands.record&&before.budget.latest->number==1&&r.experiment().budget.latest->number==2);}
        r.session->stop();CHECK(r.view().done&&r.view().reaped&&!r.view().retryable);
    }
}
void measuredWithdrawal(){using A=GameWorkSampleAdmission;
    for(unsigned mode=0;mode<5;++mode){Rig r(2);r.measurements();CHECK(r.start("withheld")&&r.baselineReady());CHECK(r.feed(0)&&r.feed(1));r.step();
        CHECK(r.until([&]{return r.lines[0].find("ECONACCEPTED")!=std::string::npos;}));const auto before=r.experiment();
        CHECK(before.commands.pending&&before.budget.attached&&before.budget.attempts==1&&!before.commands.record);
        if(mode==0){auto unsafe=r.fresh(r.now());unsafe.safety.safety.thermalSafe=false;r.step(&unsafe);}
        if(mode==1){CHECK(r.session->setEconomicContext(0,1,true));r.step();}
        if(mode==2){r.intent.stop();r.step();}
        if(mode==3)r.session->requestStop();
        if(mode==4)CHECK(r.session->submitExperimentSample(0,before.binding,99,r.sample(r.now()),r.now())==A::Invalid);
        CHECK(r.session->state()==(mode==2||mode==3?F::Stopped:F::Faulted));
        for(unsigned i=0;i<2;++i)CHECK(r.view(i).revoked&&r.experiment(i).commands.revokeRequired&&r.experiment(i).budget.attempts==1&&r.experiment(i).budget.attached);
        r.session->stop();for(unsigned i=0;i<2;++i)CHECK(r.view(i).reaped&&r.view(i).done&&!r.view(i).retryable&&!r.launch(i));}
    {Rig r(2);r.measurements();for(auto &p:r.policies)p.plan.measurementMs=500;r.reset();CHECK(r.start("exit-error")&&r.baselineReady());
        CHECK(!r.measure()&&r.session->state()==F::Faulted);r.session->stop();for(unsigned i=0;i<2;++i)CHECK(r.view(i).reaped&&!r.view(i).retryable&&r.view(i).revoked);}
    {Rig r;r.measurements();CHECK(r.start()&&r.baselineReady());auto t=r.now();auto s=r.sample(t);s.utcMs-=1001;s.monotonicMs-=1001;
        CHECK(r.session->submitExperimentSample(0,r.experiment().binding,1,s,t)==A::Accepted);r.step();CHECK(r.session->state()==F::Faulted&&!r.experiment().started&&r.experiment().budget.attempts==0);r.session->stop();}
    for(unsigned mode=0;mode<4;++mode){Rig r;r.measurements();auto &e=*r.policies[0].experiment;
        if(mode==0)e.commands.enabled=false;
        if(mode==1)e.commands.commandLeaseMs=100;
        if(mode==2)e.budget.enabled=false;
        if(mode==3)e.budget.maximumReservedEnergy.numerator=d("0");
        r.reset();CHECK(!r.start()&&r.session->state()==F::Faulted&&!r.view().requested);r.session->stop();}
}
}
int main(int argc,char **argv){if(argc>2&&std::string(argv[1])=="--economic-child")return child(argc,argv);
    if(argc!=3&&argc!=4)return 2;
    identity=std::filesystem::absolute(argv[1]);detector=std::filesystem::absolute(argv[2]);exe=std::filesystem::absolute(argv[0]);
    const std::string suite=argc==4?argv[3]:"all";
    if(suite=="all"||suite=="commands")nativeCommands();
    if(suite=="all"||suite=="withdrawal")pendingWithdrawal();
    if(suite=="all"||suite=="replacement")replacement();
    if(suite=="all"||suite=="admission")admissionAndTickets();
    if(suite=="all"||suite=="measurement")measuredSession();
    if(suite=="all"||suite=="measurement-replacement")measuredReplacement();
    if(suite=="all"||suite=="measurement-withdrawal")measuredWithdrawal();
    return finishTests("native economic command sessions");
}
