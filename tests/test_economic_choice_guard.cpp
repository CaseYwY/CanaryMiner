// Actual CPU evaluator and worker issuer/gate; no process, device, provider or mining authority.
#include "economic_fixture.h"
#include "product/economic_choice_guard.h"
#include "product_test_util.h"
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace economic_fixture;
namespace {
using S=EconomicGuardState;using A=EconomicGuardAction;using R=EconomicGuardReason;using D=WorkerDirective;using K=WorkerReceiptKind;
OptimizationTime at(uint64_t ms=0){return {T+static_cast<int64_t>(ms),1000000+ms};}
EconomicGuardPolicy policy(){EconomicGuardPolicy p;p.enabled=p.margins.enabled=p.result.enabled=true;p.result.maximumResultAgeMs=5000;
    p.confirmationMs=1000;p.minimumEvaluations=2;p.maximumConfirmationGapMs=2000;p.cooldownMs=2000;p.transitionWindowMs=10000;p.maximumTransitions=2;return p;}
EconomicEvaluationPolicy evaluator(){EconomicEvaluationPolicy p;p.enabled=p.schedule.enabled=p.result.enabled=true;
    p.schedule.evaluationMs=1000;p.schedule.operationTimeoutMs=500;p.result.maximumResultAgeMs=5000;return p;}
WorkerControlBinding binding(){return {"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0};}
struct Fixture {
    SessionIntent intent;SessionToken token=intent.explicitStart();OptimizationTime now=at();EconomicInput base=input();
    WorkProfilePlan plan=base.currentPlan;WorkProfileConditions conditions=base.conditions;uint64_t revision=1;unsigned installed=77;
    WorkerControlIssuer issuer{binding(),intent,token};WorkerControlGate gate{binding(),[this]{return now.monotonicMs;}};
    EconomicEvaluationOwner owner{evaluator(),intent,token};std::unique_ptr<EconomicChoiceGuard> guard;
    explicit Fixture(EconomicGuardPolicy p=policy()){
        base.generation=token.epoch;prices(base);CHECK(owner.start(now)&&issuer.start(now.monotonicMs));
        const auto first=issuer.issue(D::Pause,now.monotonicMs,5000);CHECK(first&&gate.receive(*first)==WorkerCommandResult::Accepted);
        gate.pauseBoundary(true);CHECK(issuer.acknowledge(*gate.snapshot().receipt,now.monotonicMs)==WorkerReceiptResult::Settled);
        guard=std::make_unique<EconomicChoiceGuard>(p,binding(),owner,issuer,intent,token);CHECK(guard->start(now,plan));
    }
    void begin(uint64_t ms,const char *newPrice=nullptr){now=at(ms);
        if(newPrice){++revision;base.nowUtcMs=now.utcMs;base.nowMonoMs=now.monotonicMs;prices(base,newPrice);}
        const auto request=owner.poll(now,revision,plan,conditions).requests[1];CHECK(request.has_value());if(!request)return;
        auto in=base;in.currentPlan=plan;in.conditions=conditions;in.nowUtcMs=request->issued.utcMs;in.nowMonoMs=request->issued.monotonicMs;
        // Rebase only the future flat-cost projection, retaining the same declared tariff identity/rules and source evidence.
        in.electricity.opening.throughUtcMs=in.nowUtcMs+1;
        in.electricity.opening.timeDatabaseVersion=civilAt(in.electricity.plan.clock,in.nowUtcMs+1).databaseVersion;
        CHECK(owner.submit(*request,revision,std::move(in),now)==EconomicSubmission::Accepted);
    }
    void finish(uint64_t ms,bool available=true){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);now=at(ms);
        do{owner.poll(now,revision,plan,conditions);if(!owner.snapshot().pending)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<until);
        CHECK(!owner.snapshot().pending&&(owner.snapshot().study!=nullptr)==available);
    }
    void study(uint64_t ms,const char *newPrice=nullptr){begin(ms,newPrice);finish(ms+1);}
    const EconomicGuardSnapshot &check(uint64_t ms){now=at(ms);return guard->check(now,revision,plan,conditions);}
    WorkerCommand send(uint64_t ms,D directive,std::optional<unsigned> wait=std::nullopt){now=at(ms);const auto c=issuer.issue(directive,now.monotonicMs,5000,wait);
        CHECK(c&&gate.receive(*c)==WorkerCommandResult::Accepted);return *c;}
    void settle(uint64_t ms){now=at(ms);const auto c=gate.snapshot().receipt->command;
        gate.pauseBoundary(c.directive==D::Pause);
        if(c.directive==D::Run)CHECK(gate.applyWorkWait(installed)==WorkControlRequest::Run);
        CHECK(issuer.acknowledge(*gate.snapshot().receipt,now.monotonicMs)==WorkerReceiptResult::Settled);
    }
    void apply(uint64_t ms,D directive,std::optional<unsigned> wait=std::nullopt){send(ms,directive,wait);settle(ms);}
};
void confirmation(){
    {Fixture f;f.study(0);auto s=f.check(1);CHECK(s.state==S::Running&&s.action==A::Pause&&s.reason==R::Confirming&&s.confirmations==1&&s.referenceOnly);
        CHECK(f.check(500).confirmations==1&&f.check(900).action==A::Pause);
        f.study(1000);s=f.check(1001);CHECK(s.action==A::Resume&&s.point==0&&s.hostWaitMs==0&&s.confirmations==2&&s.validUntilMonoMs==at(3001).monotonicMs);
        s=f.check(4000);CHECK(s.action==A::Pause&&s.reason==R::Confirming&&s.confirmations==0); // cached result cannot fill the gap
        f.study(4000);CHECK(f.check(4001).confirmations==1);f.apply(4500,D::Pause);CHECK(f.check(4500).confirmations==1);
        f.study(5000);CHECK(f.check(5001).action==A::Resume);}
    {Fixture f;f.study(0);f.check(1);f.study(1000,"0.005");CHECK(f.check(1001).confirmations==1); // context transition resets confirmation
        f.study(2000);const auto s=f.check(2001);CHECK(s.action==A::Resume&&s.point==1&&s.hostWaitMs==10);}
    {Fixture f;f.study(0);f.check(1);f.conditions.gameActive=true;CHECK(f.check(2).action==A::Pause&&f.check(2).reason==R::Unsafe);
        f.conditions.gameActive=false;const auto s=f.check(3);CHECK(s.action==A::Pause&&s.reason==R::Missing&&s.confirmations==0);
        f.study(1000);CHECK(f.check(1001).confirmations==1);f.study(2000);CHECK(f.check(2001).action==A::Resume);}
}
void appliedAndBudgets(){auto p=policy();p.confirmationMs=0;p.minimumEvaluations=1;
    {Fixture f(p);f.study(0);CHECK(f.check(1).action==A::Resume);
        const auto issued=f.send(2,D::Run,0);CHECK(f.issuer.acknowledge(*f.gate.snapshot().receipt,f.now.monotonicMs)==WorkerReceiptResult::Waiting);
        auto s=f.check(3);CHECK(s.action==A::None&&s.reason==R::Pending&&s.transitionsInWindow==0&&!s.point);
        f.settle(4);s=f.check(4);CHECK(s.action==A::None&&s.reason==R::PostApplicationStudy&&s.transitionsInWindow==1);
        CHECK(issued.issuedMonoMs!=f.gate.snapshot().receipt->observedMonoMs);
        f.study(1000,"0.005");s=f.check(1001);CHECK(s.action==A::Hold&&s.hostWaitMs==0&&s.reason==R::Cooldown);
        CHECK(f.check(2003).reason==R::Cooldown);CHECK(f.check(2004).action==A::Switch); // original application, not issue time
        f.apply(2005,D::Run,10);CHECK(f.check(2005).transitionsInWindow==2);
        f.study(3000,"0.01");CHECK(f.check(3001).reason==R::Cooldown);
        s=f.check(4005);CHECK(s.action==A::Hold&&s.hostWaitMs==10&&s.reason==R::Budget);
        f.apply(4900,D::Run,10);CHECK(f.check(4900).transitionsInWindow==2);
        f.study(6000);CHECK(f.check(6001).reason==R::Budget);
        f.apply(9000,D::Run,10);f.check(9000);f.study(10000);CHECK(f.check(10001).reason==R::Budget);
        s=f.check(10004);CHECK(s.action==A::Switch&&s.hostWaitMs==0&&s.transitionsInWindow==1); // renewals did not restart cooldown/window
    }
    {Fixture f(p);f.study(0);f.check(1);f.apply(2,D::Run,0);f.check(2);f.apply(3,D::Pause);f.check(3);
        f.study(1000);CHECK(f.check(1001).action==A::Pause&&f.check(1001).reason==R::Cooldown);
        f.apply(1002,D::Run,0);CHECK(f.check(1002).transitionsInWindow==2); // resume counts even at the same wait
        f.apply(1003,D::Run,10);CHECK(f.check(1003).state==S::Faulted);}
    {Fixture f(p);f.study(0);f.check(1);f.apply(2,D::Run);auto s=f.check(2);CHECK(s.action==A::Pause&&s.reason==R::UnknownApplication&&!s.point);}
    {Fixture f(p);f.study(0);f.check(1);f.apply(2,D::Pause);f.apply(3,D::Pause);CHECK(f.check(3).state==S::Faulted);}
}
void expiryAndStop(){
    {auto p=policy();p.result.maximumResultAgeMs=50;Fixture f(p);f.study(0);f.check(1);const auto s=f.check(50);
        CHECK(s.action==A::Pause&&!s.point&&!s.validUntilMonoMs);}
    {Fixture f;f.study(0);f.check(1);auto now=at(100);now.utcMs+=100;
        const auto first=f.guard->check(now,1,f.plan,f.conditions);CHECK(first.validUntilMonoMs==at(4900).monotonicMs);
        CHECK(f.check(300).validUntilMonoMs==at(4900).monotonicMs);CHECK(f.check(4900).action==A::Pause&&!f.guard->snapshot().validUntilMonoMs);}
    {Fixture f;f.study(0);f.check(1);CHECK(f.check(5000).state==S::Faulted&&!f.guard->snapshot().point);}
    for(unsigned mode=0;mode<4;++mode){Fixture f;f.study(0);f.check(1);auto now=at(2);
        if(mode==0)now.utcMs=T;
        if(mode==1)now.monotonicMs=at().monotonicMs;
        if(mode==2)now.utcMs+=2001;
        if(mode==3)now.utcMs=253402300800000LL;
        CHECK(f.guard->check(now,1,f.plan,f.conditions).state==S::ClockFault);CHECK(f.check(3).state==S::ClockFault);}
    for(unsigned mode=0;mode<4;++mode){Fixture f;f.study(0);f.check(1);
        if(mode==0)f.intent.stop();
        if(mode==1)f.guard->requestStop();
        if(mode==2)f.owner.requestStop();
        if(mode==3)f.issuer.requestStop();
        CHECK(f.check(2).state==S::Stopped&&!f.guard->snapshot().point);f.intent.stop();f.intent.explicitStart();
        CHECK(f.check(3).state==S::Stopped&&!f.guard->start(at(3),f.plan));}
}
void originalBoundaries(){auto p=policy();p.confirmationMs=p.cooldownMs=0;p.minimumEvaluations=1;
    {auto shortGap=p;shortGap.maximumConfirmationGapMs=50;Fixture f(shortGap);f.begin(0);f.finish(100);
        const auto s=f.check(100);CHECK(s.action==A::Pause&&s.reason==R::Confirming&&s.confirmations==0);
        f.study(1000);CHECK(f.check(1001).action==A::Resume);}
    {auto limited=p;limited.maximumTransitions=1;limited.transitionWindowMs=1000;Fixture f(limited);
        f.study(0);f.check(1);f.apply(2,D::Run,0);f.check(2);f.send(900,D::Run,10);
        f.gate.pauseBoundary(false);CHECK(f.gate.applyWorkWait(f.installed)==WorkControlRequest::Run);const auto original=*f.gate.snapshot().receipt;
        CHECK(f.check(1050).reason==R::Pending&&f.guard->snapshot().transitionsInWindow==0);
        f.now=at(1100);CHECK(f.issuer.acknowledge(original,f.now.monotonicMs)==WorkerReceiptResult::Settled);
        CHECK(f.check(1100).state==S::Faulted); // exceeded the budget at application even though delivery was delayed
    }
    {Fixture f(p);f.study(0);f.check(1);f.begin(1000);f.conditions.gameActive=true;
        CHECK(f.check(1000).reason==R::Unsafe);f.conditions.gameActive=false;f.finish(1001);
        const auto s=f.check(1001);CHECK(s.action==A::Pause&&s.reason==R::Missing&&s.confirmations==0);
        f.study(2000);CHECK(f.check(2001).action==A::Resume);}
    {Fixture f(p);f.study(0);f.check(1);f.begin(1000);f.finish(1000);f.apply(1000,D::Run,0);
        CHECK(f.check(1000).reason==R::PostApplicationStudy&&!f.guard->snapshot().point);
        f.study(2000);CHECK(f.check(2001).action==A::Hold);}
    {Fixture f;f.study(0);f.check(1);f.send(100,D::Pause);CHECK(f.check(101).reason==R::Pending&&f.guard->snapshot().confirmations==1);
        f.settle(102);CHECK(f.check(102).confirmations==1);f.study(1000);CHECK(f.check(1001).action==A::Resume);}
    {Fixture f(p);f.study(0);f.check(1);f.send(2,D::Run,0);f.conditions.thermalSafe=false;
        const auto s=f.check(3);CHECK(s.action==A::None&&s.reason==R::Unsafe&&!s.point&&!s.validUntilMonoMs);
        f.conditions.thermalSafe=true;f.settle(4);CHECK(f.check(4).action==A::Pause&&f.guard->snapshot().reason==R::Missing);}
    {Fixture f(p);f.study(0);f.check(1);f.send(2,D::Run,11);CHECK(f.check(2).state==S::Faulted);}
    {Fixture f(p);f.study(0);f.check(1);f.study(1000);f.begin(2000);
        CHECK(f.check(2000).action==A::Pause&&f.guard->snapshot().reason==R::Missing); // skipped replacement cannot retain an older cached result
        f.finish(2001);CHECK(f.check(2001).action==A::Resume);}
    {auto extended=p;extended.maximumConfirmationGapMs=10000;Fixture f(extended);f.study(0);const auto previous=f.check(1).resultSequence;
        auto skewed=at(100);skewed.utcMs+=100;f.owner.poll(skewed,f.revision,f.plan,f.conditions);f.begin(1000);
        const auto &source=f.owner.snapshot();CHECK(source.pending&&!source.study&&source.retainedResultSequence==previous&&source.retainedValidUntilMonoMs==at(4900).monotonicMs);
        auto s=f.check(1000);CHECK(s.action==A::Resume&&s.resultSequence==previous&&s.validUntilMonoMs==at(4900).monotonicMs&&s.confirmations==1);
        s=f.check(1100);CHECK(s.action==A::Resume&&s.confirmations==1&&s.validUntilMonoMs==at(4900).monotonicMs);
        f.finish(1101);CHECK(!f.owner.snapshot().retainedResultSequence&&!f.owner.snapshot().retainedValidUntilMonoMs);
    }
}
void refusals(){Fixture f;
    for(unsigned mode=0;mode<14;++mode){auto p=policy();
        if(mode==0)p.margins.enabled=false;
        if(mode==1)p.result.enabled=false;
        if(mode==2)p.result.maximumResultAgeMs=0;
        if(mode==3)p.result.maximumResultAgeMs=86400001;
        if(mode==4)p.confirmationMs=3600001;
        if(mode==5)p.maximumConfirmationGapMs=0;
        if(mode==6)p.maximumConfirmationGapMs=86400001;
        if(mode==7)p.cooldownMs=86400001;
        if(mode==8)p.transitionWindowMs=999;
        if(mode==9)p.transitionWindowMs=86400001;
        if(mode==10)p.minimumEvaluations=0;
        if(mode==11)p.minimumEvaluations=33;
        if(mode==12)p.maximumTransitions=0;
        if(mode==13)p.maximumTransitions=257;
        EconomicChoiceGuard guard(p,binding(),f.owner,f.issuer,f.intent,f.token);CHECK(!validEconomicGuardPolicy(p)&&!guard.start(at(),f.plan)&&guard.snapshot().state==S::Faulted);}
    {EconomicChoiceGuard off({},binding(),f.owner,f.issuer,f.intent,f.token);CHECK(!off.start(at(),f.plan)&&off.snapshot().state==S::Disabled);}
    {EconomicChoiceGuard stopped(policy(),binding(),f.owner,f.issuer,f.intent,f.token);stopped.requestStop();CHECK(!stopped.start(at(),f.plan)&&stopped.snapshot().state==S::Stopped);}
    {auto b=binding();b.workerKey[0]='3';EconomicChoiceGuard wrong(policy(),b,f.owner,f.issuer,f.intent,f.token);CHECK(!wrong.start(at(),f.plan));}
    {SessionIntent other;const auto token=other.explicitStart();EconomicEvaluationOwner owner(evaluator(),other,token);CHECK(owner.start(at()));
        CHECK(f.owner.boundTo(f.intent,f.token)&&!f.owner.boundTo(other,token)&&!f.owner.boundTo(f.intent,{}));
        CHECK(f.issuer.boundTo(f.intent,f.token)&&!f.issuer.boundTo(other,token)&&!f.issuer.boundTo(f.intent,{}));
        EconomicChoiceGuard wrong(policy(),binding(),owner,f.issuer,f.intent,f.token);CHECK(!wrong.start(at(),f.plan));
        EconomicChoiceGuard wrongWorker(policy(),binding(),owner,f.issuer,other,token);CHECK(!wrongWorker.start(at(),f.plan));}
    {Fixture x;x.apply(1,D::Pause);EconomicChoiceGuard reset(policy(),binding(),x.owner,x.issuer,x.intent,x.token);
        CHECK(!reset.start(at(1),x.plan)&&reset.snapshot().state==S::Faulted);}
    for(unsigned mode=0;mode<4;++mode){Fixture x;x.study(0);x.check(1);
        if(mode==0)x.revision=0;
        if(mode==1)++x.revision; // evaluator not advanced: incoherent current context
        if(mode==2)++x.plan.hostWaitMs[1];
        if(mode==3)x.conditions.fault=true;
        CHECK(x.check(2).state==S::Faulted&&!x.guard->snapshot().point);}
}
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
std::mutex buildMutex;std::condition_variable buildWake;bool held=false,entered=false,released=false;
struct BlockBuild {
    BlockBuild(){std::lock_guard<std::mutex> lock(buildMutex);held=true;entered=released=false;}
    bool reached(){std::unique_lock<std::mutex> lock(buildMutex);return buildWake.wait_for(lock,std::chrono::seconds(5),[]{return entered;});}
    void release(){std::lock_guard<std::mutex> lock(buildMutex);released=true;buildWake.notify_all();}
    ~BlockBuild(){std::lock_guard<std::mutex> lock(buildMutex);released=true;held=false;buildWake.notify_all();}
};
void pendingOwnership(){auto p=policy();p.confirmationMs=0;p.minimumEvaluations=1;
    for(unsigned mode=0;mode<4;++mode){Fixture f(p);f.study(0);f.check(1);BlockBuild block;f.begin(1000);CHECK(block.reached());
        CHECK(f.check(1000).action==A::Resume&&f.guard->snapshot().confirmations==1);
        uint64_t when=1001;
        if(mode==0)when=1500; // actual calculation is still held at timeout
        if(mode==1)f.conditions.gameActive=true;
        if(mode==2)++f.revision;
        if(mode==3)f.owner.requestStop();
        f.owner.poll(at(when),f.revision,f.plan,f.conditions);f.conditions=f.base.conditions;
        CHECK(f.owner.snapshot().pending&&f.owner.snapshot().cancellationRequested&&!f.owner.snapshot().retainedResultSequence&&!f.owner.snapshot().retainedValidUntilMonoMs);
        const auto s=f.check(when);CHECK(mode==3?s.state==S::Stopped:s.action==A::Pause&&!s.point&&!s.validUntilMonoMs);
        block.release();f.finish(when+1,false);CHECK(!f.owner.snapshot().study&&!f.guard->snapshot().point);
    }
}
#endif
}
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
namespace om {
std::shared_ptr<const EconomicStudy> economicEvaluationTestBuild(const EconomicInput &in,EconomicStatus *status,const std::atomic<bool> *cancel){
    bool ignore=false;
    {std::unique_lock<std::mutex> lock(buildMutex);if(held){ignore=true;entered=true;buildWake.notify_all();
        if(!buildWake.wait_for(lock,std::chrono::seconds(5),[]{return released;}))throw std::runtime_error("synthetic guard computation barrier");}}
    return buildEconomicStudy(in,status,ignore?nullptr:cancel);
}
}
#endif
int main(){confirmation();appliedAndBudgets();expiryAndStop();originalBoundaries();refusals();
#if defined(CANARY_ECONOMIC_EVALUATION_TESTING)
    pendingOwnership();return finishTests("economic controlled temporal references");
#else
    return finishTests("economic receipt-bound temporal references");
#endif
}
