// Parent reservation accounting through actual experiment capture/commands and one-use economic transfer. Synthetic only.
#include "economic_fixture.h"
#include "product/work_experiment_budget.h"
#include "product/work_experiment_owner.h"
#include "product/economic_command_owner.h"
#include "product_test_util.h"
using namespace economic_fixture;
namespace {
using S=WorkExperimentBudgetState;using R=WorkProfileStatus;using D=WorkerDirective;using A=WorkerReceiptResult;
WorkMeasurementTime at(uint64_t ms=0){return {T+static_cast<int64_t>(ms),1000000+ms};}
OptimizationTime opt(uint64_t ms=0){return {at(ms).utcMs,at(ms).monotonicMs};}
WorkProfilePlan plan(){auto p=profile()->input().plan;p.hostWaitMs={0};p.telemetryCadenceMs=5;p.maximumSampleGapMs=50;
    p.warmupMs=0;p.measurementMs=10;p.repetitions=1;p.maximumElapsedMs=100;p.maximumEnergy.numerator=d("10000");return p;}
WorkExperimentBudgetPolicy policy(){WorkExperimentBudgetPolicy p;p.enabled=true;p.maximumAttempts=2;p.minimumRetestMs=100;
    p.maximumReservedElapsedMs=200;p.maximumReservedEnergy.numerator=d("20000");return p;}
EconomicCommandPolicy economic(){EconomicCommandPolicy p;p.enabled=p.guard.enabled=p.guard.margins.enabled=p.guard.result.enabled=true;
    p.commandLeaseMs=1000;p.guard.cooldownMs=0;return p;}
EconomicEvaluationPolicy evaluation(){EconomicEvaluationPolicy p;p.enabled=p.schedule.enabled=p.result.enabled=true;return p;}
std::string workerKey(unsigned generation,char custom){if(custom)return std::string(32,custom);
    const auto value=std::to_string(generation);return std::string(32-value.size(),'0')+value;}
struct Parent {
    SessionIntent intent;SessionToken token=intent.explicitStart();WorkProfilePlan settings=plan();
    WorkExperimentBudget budget;EconomicParentBudget economicBudget;EconomicEvaluationOwner evaluator;
    explicit Parent(WorkExperimentBudgetPolicy p=policy()):budget(p,settings,intent,token),economicBudget({true,0,3600000,6,256},intent,token),evaluator(evaluation(),intent,token){
        CHECK(budget.start(at())&&economicBudget.start(opt())&&evaluator.start(opt()));}
};
struct Life {
    Parent &parent;uint64_t offset;WorkMeasurementTime now;WorkerControlBinding binding;
    WorkerControlIssuer issuer;WorkerControlGate gate;WorkProfileConditions conditions{true,true,true,false,false,false};unsigned installed=0;
    WorkExperimentOwner experiment;EconomicCommandOwner economy;
    Life(Parent &p,unsigned generation,uint64_t start,char key=0):parent(p),offset(start),now(at(start)),
        binding{std::string(32,'1'),workerKey(generation,key),p.token.epoch,generation,0},
        issuer(binding,p.intent,p.token),gate(binding,[&]{return now.monotonicMs;}),
        experiment({true,100,5,500},p.settings,binding,issuer,p.intent,p.token,&p.budget),economy(economic(),binding,p.evaluator,issuer,p.economicBudget,p.intent,p.token){
        CHECK(issuer.start(now.monotonicMs));const auto initial=issuer.issue(D::Pause,now.monotonicMs,5000);CHECK(initial&&gate.receive(*initial)==WorkerCommandResult::Accepted);
        gate.pauseBoundary(true);CHECK(issuer.acknowledge(*gate.snapshot().receipt,now.monotonicMs)==A::Settled);
    }
    WorkProfileSample sample(uint64_t elapsed)const{WorkProfileSample s;s.generation=parent.token.epoch;s.counterEpoch=1;s.fingerprint=workProfileFingerprint(parent.settings.identity);
        s.utcMs=at(offset+elapsed).utcMs;s.monotonicMs=at(offset+elapsed).monotonicMs;s.appliedHostWaitMs=installed;
        s.completeWorkBoundary=s.safetyTelemetryValid=s.thermalSafe=s.gameStateKnown=true;s.milliwatts=100000;s.userTmacs=d(std::to_string(elapsed));s.donationTmacs=d("0");return s;}
    bool start(){return experiment.start(sample(0),now,conditions,UINT64_MAX);}
    WorkerCommand send(uint64_t elapsed){now=at(offset+elapsed);const auto command=experiment.poll(now,conditions,UINT64_MAX);CHECK(command.has_value());if(!command)return {};
        CHECK(gate.receive(*command)==WorkerCommandResult::Accepted);return *command;}
    void apply(uint64_t elapsed){now=at(offset+elapsed);const auto r=gate.snapshot().receipt;CHECK(r.has_value());if(!r)return;
        gate.pauseBoundary(r->command.directive==D::Pause);if(r->command.directive==D::Run)CHECK(gate.applyWorkWait(installed)==WorkControlRequest::Run);
        CHECK(experiment.acknowledge(*gate.snapshot().receipt,now,conditions,UINT64_MAX)==A::Settled);}
    bool finish(bool transfer=true){send(1);apply(2);now=at(offset+5);CHECK(experiment.append(2,sample(5),now,conditions,UINT64_MAX));
        now=at(offset+15);CHECK(experiment.append(3,sample(15),now,conditions,UINT64_MAX));send(16);apply(17);now=at(offset+20);
        CHECK(experiment.append(4,sample(20),now,conditions,UINT64_MAX));return !transfer||economy.startAfterMeasurement(opt(offset+21),parent.settings,experiment);}
    bool retire(){now=at(offset+22);issuer.requestStop();return economy.retire(opt(offset+22))&&parent.budget.retire(issuer,now);}
};
void totalsAndExhaustion(){Parent p;Life first(p,1,0);CHECK(first.start());const auto original=p.budget.snapshot().latest;
    CHECK(original&&original->number==1&&original->elapsedMs==100&&original->energy.numerator.text()=="10000");
    CHECK(p.budget.snapshot().attempts==1&&p.budget.snapshot().reservedElapsedMs==100&&p.budget.snapshot().observedElapsedMs==0);
    CHECK(first.finish());CHECK(p.budget.snapshot().completed&&p.budget.snapshot().transferred&&p.budget.snapshot().attached);
    CHECK(p.budget.snapshot().observedElapsedMs==20&&p.budget.snapshot().observedEnergy.numerator.text()=="2000"&&p.budget.snapshot().reservedEnergy.numerator.text()=="10000");
    CHECK(first.retire()&&!p.budget.snapshot().attached&&p.budget.snapshot().reservedElapsedMs==100);
    Life second(p,2,100);CHECK(second.start()&&second.finish()&&second.retire());
    CHECK(p.budget.snapshot().attempts==2&&p.budget.snapshot().reservedElapsedMs==200&&p.budget.snapshot().observedElapsedMs==40&&
        p.budget.snapshot().reservedEnergy.numerator.text()=="20000"&&p.budget.snapshot().observedEnergy.numerator.text()=="4000");
    CHECK(original->number==1&&original->admitted.monotonicMs==at().monotonicMs&&p.budget.snapshot().latest!=original);
    Life third(p,3,200);CHECK(!third.start()&&third.experiment.snapshot().status==R::Budget&&third.experiment.snapshot().revokeRequired);
    CHECK(p.budget.snapshot().attempts==2&&!p.budget.snapshot().attached&&!p.budget.start(at(200)));
}
void independentLimits(){
    for(unsigned mode=0;mode<3;++mode){auto settings=policy();if(mode==0)settings.maximumReservedElapsedMs=199;
        if(mode==1)settings.maximumReservedEnergy.numerator=d("19999.999999999999");
        if(mode==2)settings.minimumRetestMs=101;
        Parent p(settings);Life first(p,1,0);CHECK(first.start()&&first.finish()&&first.retire());Life second(p,2,100);
        CHECK(!second.start()&&second.experiment.snapshot().status==R::Budget&&p.budget.snapshot().state==S::Running&&p.budget.snapshot().attempts==1);
        CHECK(p.budget.snapshot().reservedEnergy.numerator.text()=="10000"&&!p.budget.snapshot().attached);
    }
    {auto settings=policy();settings.maximumReservedElapsedMs=99;Parent p(settings);Life first(p,1,0);
        CHECK(!first.start()&&p.budget.snapshot().attempts==0&&!first.experiment.snapshot().lastRun);}
    {Parent p;Life first(p,1,0);CHECK(first.start());first.send(1);first.experiment.transportFailed();
        CHECK(p.budget.snapshot().state==S::Faulted&&p.budget.snapshot().attempts==1&&p.budget.snapshot().reservedElapsedMs==100&&
            p.budget.snapshot().reservedEnergy.numerator.text()=="10000"&&p.budget.snapshot().observedEnergy.numerator.zero());
        first.issuer.requestStop();CHECK(!p.budget.retire(first.issuer,at(2)));Life next(p,2,100);CHECK(!next.start());}
}
void ownershipAndRetirement(){
    {Parent p;Life first(p,1,0);CHECK(first.start());Life overlap(p,2,0);CHECK(!overlap.start()&&p.budget.snapshot().state==S::Faulted);
        CHECK(!first.experiment.poll(at(1),first.conditions,UINT64_MAX)&&first.experiment.snapshot().revokeRequired);}
    {Parent p;Life first(p,1,0);CHECK(first.start()&&first.finish());CHECK(!p.budget.retire(first.issuer,at(22))&&p.budget.snapshot().attached);}
    {Parent p;Life first(p,1,0);CHECK(first.start()&&first.finish());
        const auto pending=first.issuer.issue(D::Pause,at(22).monotonicMs,1000);CHECK(pending.has_value());first.issuer.requestStop();
        CHECK(!p.budget.retire(first.issuer,at(23))&&p.budget.snapshot().state==S::Faulted);}
    {Parent p;Life first(p,1,0);CHECK(first.start());first.issuer.requestStop();CHECK(!p.budget.retire(first.issuer,at(1)));}
    {Parent p;Life first(p,1,0,'2');CHECK(first.start()&&first.finish()&&first.retire());Life reused(p,2,100,'2');CHECK(!reused.start()&&p.budget.snapshot().status==R::Attribution);}
    {Parent p;Life second(p,2,0);CHECK(!second.start()&&p.budget.snapshot().attempts==0);} // a fresh parent budget cannot adopt replacement generation
    {Parent p;Life first(p,1,0);CHECK(first.start()&&first.finish()&&first.retire());++p.settings.validForMs;Life changed(p,2,100);CHECK(!changed.start());}
    {Parent p;Life first(p,1,0);CHECK(first.start()&&first.finish());WorkExperimentBudget fresh(policy(),p.settings,p.intent,p.token);CHECK(fresh.start(at(22)));
        first.issuer.requestStop();CHECK(!fresh.retire(first.issuer,at(22))&&fresh.snapshot().attempts==0);}
    {Parent p;Life first(p,1,0);CHECK(first.start());p.budget.requestStop();CHECK(!first.experiment.poll(at(1),first.conditions,UINT64_MAX)&&
            first.experiment.snapshot().status==R::Stopped&&p.budget.snapshot().reservedElapsedMs==100);}
    {Parent p;Life first(p,1,0);CHECK(first.start());p.intent.stop();p.intent.explicitStart();CHECK(!first.experiment.poll(at(1),first.conditions,UINT64_MAX));
        CHECK(p.budget.snapshot().state==S::Stopped&&!p.budget.start(at(1)));}
    {Parent p;Life first(p,1,0);CHECK(first.start()&&first.finish()&&first.retire());Life second(p,2,100);CHECK(second.start());
        auto skew=at(101);skew.utcMs+=2001;CHECK(!second.experiment.poll(skew,second.conditions,UINT64_MAX)&&p.budget.snapshot().status==R::Clock);}
    {Parent p;Life first(p,1,0);CHECK(first.start()&&first.finish());first.issuer.requestStop();auto retired=at(22);retired.utcMs+=1000;
        CHECK(first.economy.retire({retired.utcMs,retired.monotonicMs})&&p.budget.retire(first.issuer,retired));
        Life second(p,2,100);second.now.utcMs+=1000;auto baseline=second.sample(0);baseline.utcMs+=1000;
        CHECK(second.experiment.start(baseline,second.now,second.conditions,UINT64_MAX));
        auto skew=at(101);skew.utcMs+=2001;CHECK(!second.experiment.poll(skew,second.conditions,UINT64_MAX)&&p.budget.snapshot().status==R::Clock);
    } // each local anchor would allow this skew; the retained parent anchor refuses it
    {Parent p;Life first(p,1,0);CHECK(first.start()&&first.finish(false));auto wrong=first.binding;wrong.workerKey=std::string(32,'a');
        EconomicCommandOwner successor(economic(),wrong,p.evaluator,first.issuer,p.economicBudget,p.intent,p.token);
        CHECK(!successor.startAfterMeasurement(opt(21),p.settings,first.experiment)&&successor.snapshot().revokeRequired);
        CHECK(p.budget.snapshot().state==S::Faulted&&p.budget.snapshot().transferred&&p.budget.snapshot().reservedElapsedMs==100);
        CHECK(!p.budget.retire(first.issuer,at(22)));}
    {Parent p;Life first(p,1,0);CHECK(first.start()&&first.finish());first.experiment.requestStop();first.experiment.transportFailed();
        CHECK(p.budget.snapshot().state==S::Running&&first.retire());} // stale experiment callbacks cannot stop its economic successor
    {Parent p;{Life first(p,1,0);CHECK(first.start());}CHECK(p.budget.snapshot().state==S::Stopped&&p.budget.snapshot().attached&&p.budget.snapshot().reservedElapsedMs==100);}
}
void policyRefusals(){
    for(unsigned mode=0;mode<8;++mode){SessionIntent intent;const auto token=intent.explicitStart();auto p=policy();
        if(mode==0)p.enabled=false;
        if(mode==1)p.maximumAttempts=0;
        if(mode==2)p.maximumAttempts=257;
        if(mode==3)p.maximumReservedElapsedMs=0;
        if(mode==4)p.maximumReservedEnergy.numerator=d("0");
        if(mode==5)p.maximumReservedEnergy.numerator=d("-1");
        if(mode==6)p.minimumRetestMs=2592000001ULL;
        if(mode==7)p.maximumReservedElapsedMs=256ULL*86400000+1;
        WorkExperimentBudget budget(p,plan(),intent,token);CHECK(!budget.start(at())&&!budget.start(at(1)));}
}
void boundedOriginals(){auto settings=policy();settings.maximumAttempts=256;settings.minimumRetestMs=30;
    settings.maximumReservedElapsedMs=25600;settings.maximumReservedEnergy.numerator=d("2560000");Parent parent(settings);
    std::shared_ptr<const WorkExperimentReservation> first;
    for(unsigned i=0;i<256;++i){Life life(parent,i+1,i*30);const bool started=life.start();CHECK(started);if(!started)return;
        if(!i)first=parent.budget.snapshot().latest;
        CHECK(life.finish()&&life.retire());
    }
    CHECK(parent.budget.snapshot().attempts==256&&parent.budget.snapshot().reservedElapsedMs==25600&&
        parent.budget.snapshot().observedElapsedMs==5120&&parent.budget.snapshot().reservedEnergy.numerator.text()=="2560000"&&
        parent.budget.snapshot().observedEnergy.numerator.text()=="512000");
    CHECK(first&&first->number==1&&first->initialPause.command.binding.workerGeneration==1&&parent.budget.snapshot().latest->number==256);
    Life refused(parent,257,7680);CHECK(!refused.start()&&refused.experiment.snapshot().status==R::Budget&&parent.budget.snapshot().attempts==256);
}
}
int main(){totalsAndExhaustion();independentLimits();ownershipAndRetirement();policyRefusals();boundedOriginals();return finishTests("parent-retained experiment reservations");}
