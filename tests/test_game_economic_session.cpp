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
    const std::string mode=argv[2];const auto began=workerControlNowMs();uint64_t first=0,printed=0;unsigned installed=999;int code=8;
    while(workerControlNowMs()-began<15000){const auto state=client.requestState();
        if(state==WorkControlRequest::Stop){code=0;break;}
        if(state==WorkControlRequest::Fault)break;
        if(state==WorkControlRequest::Pause){client.pauseBoundary(true);client.pauseHeld();}
        else if(state==WorkControlRequest::Run){client.pauseBoundary(false);
            if(mode!="withheld"&&mode!="exit-pending")client.applyWorkWait(installed);
        }
        const auto view=client.snapshot();
        if(view.receipt&&view.receipt->command.revision!=printed){printed=view.receipt->command.revision;
            if(view.receipt->command.directive==D::Run){
                std::puts(view.receipt->kind==K::WaitApplied?"ECONAPPLIED":"ECONACCEPTED");std::fflush(stdout);
                if(!first)first=workerControlNowMs();
            }
        }
        if(first&&workerControlNowMs()-first>=200){
            if(mode=="exit-pending"||mode=="exit-error"||(mode=="restart-once"&&view.receipt&&view.receipt->command.binding.workerGeneration==1)){code=1;break;}
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
        auto in=input();in.generation=token.epoch;in.nowUtcMs=t.issued.utcMs;in.nowMonoMs=t.issued.monotonicMs;
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
}
int main(int argc,char **argv){if(argc>2&&std::string(argv[1])=="--economic-child")return child(argc,argv);
    if(argc!=3&&argc!=4)return 2;
    identity=std::filesystem::absolute(argv[1]);detector=std::filesystem::absolute(argv[2]);exe=std::filesystem::absolute(argv[0]);
    const std::string suite=argc==4?argv[3]:"all";
    if(suite=="all"||suite=="commands")nativeCommands();
    if(suite=="all"||suite=="withdrawal")pendingWithdrawal();
    if(suite=="all"||suite=="replacement")replacement();
    if(suite=="all"||suite=="admission")admissionAndTickets();
    return finishTests("native economic command sessions");
}
