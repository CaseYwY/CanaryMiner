// Self-owned CPU peers over actual native pipes. Parent-supplied observations and all economics are explicitly synthetic.
#include "economic_fixture.h"
#include "product/work_experiment_owner.h"
#include "product/work_experiment_budget.h"
#include "product/economic_command_owner.h"
#include "product/worker_process_owner.h"
#include "product_test_util.h"
#include <thread>
using namespace economic_fixture;
namespace {
using S=WorkExperimentState;using D=WorkerDirective;using K=WorkerReceiptKind;using A=WorkerReceiptResult;
void sleep(){std::this_thread::sleep_for(std::chrono::milliseconds(5));}
bool same(const WorkerReceipt &a,const WorkerReceipt &b){return sameWorkerCommand(a.command,b.command)&&a.kind==b.kind&&a.observedMonoMs==b.observedMonoMs;}
int child(int argc,char **argv){if(argc!=7||std::string(argv[3])!="--worker-control-v1")return 90;
    WorkerChannelHandles handles;if(!parseWorkerChannelHandles(argv[4],argv[5],argv[6],&handles))return 91;
    WorkerControlClient client;if(!client.adopt(handles))return 92;
    const bool withheld=std::string(argv[2])=="withheld";unsigned installed=999;const auto began=workerControlNowMs();int code=8;
    while(workerControlNowMs()-began<15000){const auto state=client.requestState();
        if(state==WorkControlRequest::Stop){code=0;break;}
        if(state==WorkControlRequest::Fault)break;
        if(state==WorkControlRequest::Pause){client.pauseBoundary(true);client.pauseHeld();}
        else{client.pauseBoundary(false);if(!withheld)client.applyWorkWait(installed);}
        sleep();
    }
    client.stop();return code;
}
WorkProfilePlan plan(){auto p=profile()->input().plan;p.telemetryCadenceMs=10;p.maximumSampleGapMs=1000;p.warmupMs=30;p.measurementMs=100;
    p.repetitions=2;p.maximumElapsedMs=8000;p.maximumEnergy.numerator=d("10000000");return p;}
EconomicCommandPolicy commands(){EconomicCommandPolicy p;p.enabled=p.guard.enabled=p.guard.margins.enabled=p.guard.result.enabled=true;
    p.commandLeaseMs=1000;p.guard.result.maximumResultAgeMs=5000;p.guard.confirmationMs=p.guard.cooldownMs=0;p.guard.minimumEvaluations=1;
    p.guard.maximumConfirmationGapMs=5000;p.guard.transitionWindowMs=10000;p.guard.maximumTransitions=2;return p;}
EconomicEvaluationPolicy evaluations(){EconomicEvaluationPolicy p;p.enabled=p.schedule.enabled=p.result.enabled=true;
    p.schedule.evaluationMs=1000;p.schedule.operationTimeoutMs=500;p.result.maximumResultAgeMs=5000;return p;}
void run(const std::filesystem::path &exe,unsigned mode){
    SessionIntent intent;const auto token=intent.explicitStart();const auto anchor=workerControlNowMs();
    auto now=[&]{const auto ms=workerControlNowMs();return WorkMeasurementTime{T+static_cast<int64_t>(ms-anchor),ms};};
    auto optimization=[](WorkMeasurementTime t){return OptimizationTime{t.utcMs,t.monotonicMs};};
    WorkerControlBinding binding{std::string(32,'1'),std::string(32,'2'),token.epoch,1,0};WorkerControlIssuer issuer(binding,intent,token);
    const auto began=now();CHECK(issuer.start(began.monotonicMs));const auto initial=issuer.issue(D::Pause,began.monotonicMs,5000);CHECK(initial.has_value());if(!initial)return;
    WorkerProcessOwner process;CHECK(process.start(exe,{"--experiment-child",mode?"withheld":"normal"},*initial,300));
    std::optional<WorkerReceipt> consumed;bool paused=false;
    while(workerControlNowMs()-anchor<4000){const auto snapshot=process.snapshot();if(snapshot.done)break;
        if(snapshot.receipt&&snapshot.receipt->kind==K::Paused){const auto t=now();paused=issuer.acknowledge(*snapshot.receipt,t.monotonicMs)==A::Settled;consumed=snapshot.receipt;break;}sleep();}
    CHECK(paused);if(!paused){process.stop();return;}
    const auto settings=plan();WorkProfileConditions conditions{true,true,true,false,false,false};unsigned installed=0;uint64_t sequence=1;
    auto sample=[&](WorkMeasurementTime t){WorkProfileSample s;s.generation=token.epoch;s.counterEpoch=1;s.fingerprint=workProfileFingerprint(settings.identity);
        s.utcMs=t.utcMs;s.monotonicMs=t.monotonicMs;s.appliedHostWaitMs=installed;s.completeWorkBoundary=s.safetyTelemetryValid=s.thermalSafe=s.gameStateKnown=true;
        s.milliwatts=installed?100000:200000;s.userTmacs=d(std::to_string((t.monotonicMs-anchor)*2));s.donationTmacs=d(std::to_string(t.monotonicMs-anchor));return s;};
    WorkExperimentBudgetPolicy limits;limits.enabled=true;limits.maximumAttempts=2;limits.maximumReservedElapsedMs=16000;limits.maximumReservedEnergy.numerator=d("20000000");
    WorkExperimentBudget reservations(limits,settings,intent,token);CHECK(reservations.start(began));
    WorkExperimentOwner owner({true,300,80,5000},settings,binding,issuer,intent,token,&reservations);
    EconomicParentBudget budget({true,0,10000,2,4},intent,token);CHECK(budget.start(optimization(began)));
    const auto baseline=now();CHECK(owner.start(sample(baseline),baseline,conditions,baseline.monotonicMs+2000));
    uint64_t lastSample=baseline.monotonicMs;bool withdrawn=false;size_t issued=0;
    while(workerControlNowMs()-anchor<10000&&owner.snapshot().state==S::Running){const auto native=process.snapshot();const auto t=now();
        if(native.done||native.revoked){owner.transportFailed();break;}
        if(native.receipt&&(!consumed||!same(*native.receipt,*consumed))){consumed=native.receipt;
            owner.acknowledge(*native.receipt,t,conditions,t.monotonicMs+2000);
            if(native.receipt->kind==K::WaitApplied)installed=*native.receipt->command.hostWaitMs;
        }
        if(mode&&consumed&&consumed->kind==K::Accepted&&consumed->command.directive==D::Run){
            CHECK(owner.snapshot().pending&&!owner.snapshot().record);const auto deadline=consumed->command.deadlineMonoMs;
            if(mode==1)conditions.thermalSafe=false;
            if(mode==2)intent.stop();
            if(mode==3)owner.transportFailed();
            owner.poll(t,conditions,t.monotonicMs+2000);withdrawn=owner.snapshot().revokeRequired&&t.monotonicMs<deadline;break;
        }
        if(t.monotonicMs-lastSample>=settings.telemetryCadenceMs){CHECK(owner.append(++sequence,sample(t),t,conditions,t.monotonicMs+2000));lastSample=t.monotonicMs;}
        const auto command=owner.poll(t,conditions,t.monotonicMs+2000);
        if(command){++issued;if(!process.submit(*command)){owner.transportFailed();break;}}
        if(owner.snapshot().revokeRequired)break;
        sleep();
    }
    if(mode){CHECK(withdrawn&&owner.snapshot().revokeRequired&&owner.snapshot().pending);process.requestStop();}
    else{
        CHECK(owner.snapshot().state==S::Holding&&owner.snapshot().record&&issued>=3);
        if(owner.snapshot().record){const auto original=owner.snapshot().record;const auto measured=buildWorkProfile(original->measurement->input);
            CHECK(measured&&measured->summary().measuredMs>=400&&original->measurement->receipts.back().kind==K::Paused);
            EconomicEvaluationOwner evaluator(evaluations(),intent,token);auto t=now();CHECK(evaluator.start(optimization(t)));
            EconomicCommandOwner economy(commands(),binding,evaluator,issuer,budget,intent,token);
            CHECK(economy.startAfterMeasurement(optimization(t),settings,owner)&&owner.snapshot().state==S::Transferred);
            owner.requestStop();bool submitted=false,applied=false;const auto handoff=t.monotonicMs;
            while(workerControlNowMs()-handoff<4000){const auto native=process.snapshot();t=now();if(native.done||native.revoked){economy.transportFailed();break;}
                if(native.receipt&&(!consumed||!same(*native.receipt,*consumed))){consumed=native.receipt;economy.acknowledge(*consumed,optimization(t));}
                const auto schedule=evaluator.poll(optimization(t),1,settings,conditions);
                if(!submitted&&schedule.requests[1]&&schedule.requests[1]->issued.monotonicMs>handoff){const auto ticket=*schedule.requests[1];
                    auto in=input(measured);in.generation=token.epoch;in.nowUtcMs=ticket.issued.utcMs;in.nowMonoMs=ticket.issued.monotonicMs;prices(in);
                    in.electricity.opening.throughUtcMs=in.nowUtcMs+1;in.electricity.opening.timeDatabaseVersion=civilAt(in.electricity.plan.clock,in.nowUtcMs+1).databaseVersion;
                    CHECK(evaluator.submit(ticket,1,std::move(in),optimization(t))==EconomicSubmission::Accepted);submitted=true;
                }else if(!submitted&&schedule.requests[1]){
                    CHECK(evaluator.completeExternal(*schedule.requests[1],OptimizationOutcome::Rejected,optimization(t))==OptimizationCompletion::Accepted);
                }
                const auto command=economy.poll(optimization(t),1,settings,conditions,t.monotonicMs+2000);
                if(command&&!process.submit(*command)){economy.transportFailed();break;}
                if(economy.snapshot().application&&!economy.snapshot().pending){applied=true;break;}
                if(economy.snapshot().revokeRequired)break;
                sleep();
            }
            CHECK(applied&&submitted&&economy.snapshot().measurement==original&&budget.snapshot().transitionsInWindow==1);
            process.requestStop();process.stop();const auto ended=process.snapshot();CHECK(ended.done&&ended.reaped&&ended.exitCode==0);
            t=now();issuer.requestStop();CHECK(economy.retire(optimization(t))&&reservations.retire(issuer,t));evaluator.stop();
            CHECK(!reservations.snapshot().attached&&reservations.snapshot().attempts==1&&reservations.snapshot().reservedElapsedMs==settings.maximumElapsedMs);
        }
    }
    owner.requestStop();process.stop();const auto final=process.snapshot();CHECK(final.done&&final.reaped&&final.revoked&&final.exitCode==0);
    CHECK(reservations.snapshot().attempts==1&&reservations.snapshot().reservedEnergy.numerator.text()=="10000000");
    if(mode)CHECK(reservations.snapshot().state!=WorkExperimentBudgetState::Running&&reservations.snapshot().attached);
}
}
int main(int argc,char **argv){if(argc>2&&std::string(argv[1])=="--experiment-child")return child(argc,argv);
    const auto exe=std::filesystem::absolute(argv[0]);for(unsigned mode=0;mode<4;++mode)run(exe,mode);
    return finishTests("native CPU experiment command ownership");}
