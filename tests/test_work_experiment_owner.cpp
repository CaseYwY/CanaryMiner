// Actual capture/issuer/gate and CPU evaluator; all work, power and financial observations are synthetic.
#include "economic_fixture.h"
#include "product/work_experiment_owner.h"
#include "product/economic_command_owner.h"
#include "product_test_util.h"
#include <thread>
using namespace economic_fixture;
namespace {
using S=WorkExperimentState;using M=WorkMeasurementState;using R=WorkProfileStatus;using A=WorkerReceiptResult;
using D=WorkerDirective;using K=WorkerReceiptKind;
WorkMeasurementTime at(uint64_t ms=0){return {T+static_cast<int64_t>(ms),1000000+ms};}
OptimizationTime opt(uint64_t ms=0){return {at(ms).utcMs,at(ms).monotonicMs};}
WorkerControlBinding binding(){return {std::string(32,'1'),std::string(32,'2'),1,1,0};}
WorkProfilePlan plan(){auto p=profile()->input().plan;p.telemetryCadenceMs=5;p.maximumSampleGapMs=60;p.warmupMs=10;p.measurementMs=20;
    p.repetitions=2;p.maximumElapsedMs=1000;p.maximumEnergy.numerator=d("1000000");return p;}
WorkExperimentPolicy policy(){return {true,100,5,500};}
EconomicCommandPolicy economicPolicy(){EconomicCommandPolicy p;p.enabled=p.guard.enabled=p.guard.margins.enabled=p.guard.result.enabled=true;
    p.commandLeaseMs=1000;p.guard.result.maximumResultAgeMs=5000;p.guard.confirmationMs=p.guard.cooldownMs=0;p.guard.minimumEvaluations=1;
    p.guard.maximumConfirmationGapMs=5000;p.guard.transitionWindowMs=10000;p.guard.maximumTransitions=2;return p;}
EconomicEvaluationPolicy evaluatorPolicy(){EconomicEvaluationPolicy p;p.enabled=p.schedule.enabled=p.result.enabled=true;
    p.schedule.evaluationMs=1000;p.schedule.operationTimeoutMs=500;p.result.maximumResultAgeMs=5000;return p;}
struct Rig {
    SessionIntent intent;SessionToken token=intent.explicitStart();WorkProfilePlan settings=plan();WorkExperimentPolicy commands=policy();WorkMeasurementTime now=at();
    WorkerControlIssuer issuer{binding(),intent,token};WorkerControlGate gate{binding(),[&]{return now.monotonicMs;}};
    WorkProfileConditions conditions{true,true,true,false,false,false};unsigned installed=0;uint64_t sequence=1,permission=UINT64_MAX;
    std::unique_ptr<WorkExperimentOwner> owner;
    explicit Rig(bool begin=true){CHECK(issuer.start(now.monotonicMs));const auto initial=issuer.issue(D::Pause,now.monotonicMs,5000);
        CHECK(initial&&gate.receive(*initial)==WorkerCommandResult::Accepted);gate.pauseBoundary(true);
        CHECK(issuer.acknowledge(*gate.snapshot().receipt,now.monotonicMs)==A::Settled);if(begin)CHECK(start());}
    WorkProfileSample sample(uint64_t ms)const{WorkProfileSample s;s.generation=token.epoch;s.counterEpoch=1;s.fingerprint=workProfileFingerprint(settings.identity);
        s.utcMs=at(ms).utcMs;s.monotonicMs=at(ms).monotonicMs;s.appliedHostWaitMs=installed;s.completeWorkBoundary=s.safetyTelemetryValid=s.thermalSafe=s.gameStateKnown=true;
        s.milliwatts=installed?100000:200000;s.userTmacs=d(std::to_string(ms*2));s.donationTmacs=d(std::to_string(ms));return s;}
    bool start(){owner=std::make_unique<WorkExperimentOwner>(commands,settings,binding(),issuer,intent,token);return owner->start(sample(0),now,conditions,permission);}
    std::optional<WorkerCommand> poll(uint64_t ms){now=at(ms);return owner->poll(now,conditions,permission);}
    WorkerCommand send(uint64_t ms){const auto c=poll(ms);CHECK(c.has_value());if(!c)return {};
        CHECK(gate.receive(*c)==WorkerCommandResult::Accepted);CHECK(owner->acknowledge(*gate.snapshot().receipt,now,conditions,permission)==A::Waiting);return *c;}
    WorkerReceipt apply(uint64_t ms,bool deliver=true){now=at(ms);const auto pending=gate.snapshot().receipt;
        CHECK(pending.has_value());if(!pending)return {};
        gate.pauseBoundary(pending->command.directive==D::Pause);if(pending->command.directive==D::Run)CHECK(gate.applyWorkWait(installed)==WorkControlRequest::Run);
        const auto r=*gate.snapshot().receipt;if(deliver)CHECK(owner->acknowledge(r,now,conditions,permission)==A::Settled);return r;}
    bool add(uint64_t ms){now=at(ms);return owner->append(++sequence,sample(ms),now,conditions,permission);}
    bool complete(){
        const auto first=send(1);CHECK(first.hostWaitMs==0&&first.deadlineMonoMs==at(60).monotonicMs);apply(2);
        CHECK(add(5)&&add(15)&&add(35)&&add(45)&&add(55));const auto renewal=send(55);CHECK(renewal.hostWaitMs==0);
        CHECK(add(65)&&owner->measurement().requestedHostWaitMs==10);CHECK(!poll(65)&&owner->snapshot().pending);apply(66);
        const auto changed=send(66);CHECK(changed.hostWaitMs==10);apply(67);
        CHECK(add(70)&&add(80)&&add(100)&&add(110)&&add(120));send(120);
        CHECK(add(130)&&owner->measurement().state==M::Draining);CHECK(!poll(130)&&owner->snapshot().pending);apply(131);
        const auto pause=send(131);CHECK(pause.directive==D::Pause&&!owner->snapshot().record);apply(132);
        CHECK(add(135));return owner->snapshot().state==S::Holding;
    }
};
void fullExperiment(){Rig r;CHECK(r.complete());const auto record=r.owner->snapshot().record;CHECK(record&&record->runs.size()==4);
    if(!record)return;
    const auto &m=*record->measurement;const auto p=buildWorkProfile(m.input);
    CHECK(p&&p->summary().elapsedMs==135&&p->summary().measuredMs==80&&p->summary().overheadMs==55&&m.receipts.size()==6);
    CHECK(m.receipts.back().observedMonoMs==at(132).monotonicMs&&m.completed.monotonicMs==at(135).monotonicMs);
    CHECK(record->runs[0]->source.monotonicMs==at().monotonicMs&&record->runs[0]->sequence==1&&record->runs[0]->requestRevision==1&&
        record->runs[1]->requestRevision==1&&record->runs[2]->requestRevision==2&&record->runs[3]->requestRevision==2);
    CHECK(record->runs[0]->plan==record->runs[3]->plan&&workProfilePlanFingerprint(*record->runs[0]->plan)==workProfilePlanFingerprint(r.settings));
    CHECK(r.owner->measurement().requestRevision==3&&r.owner->measurement().latestSample->monotonicMs==at(135).monotonicMs);
    CHECK(!r.poll(200)&&r.owner->snapshot().state==S::Holding);const auto renewed=r.send(226);CHECK(renewed.directive==D::Pause);r.apply(227);
    CHECK(r.owner->snapshot().record==record&&record->measurement->receipts.size()==6&&r.owner->snapshot().handoffUntilMonoMs==at(635).monotonicMs);
    r.owner->requestStop();CHECK(r.owner->snapshot().revokeRequired&&r.owner->snapshot().record==record&&!r.poll(228));
}
void constraintsAndLate(){
    for(unsigned mode=0;mode<12;++mode){Rig r;const auto c=r.send(1);const auto original=r.owner->snapshot().lastRun;const auto receipt=r.apply(2,false);
        if(mode==0)r.conditions.thermalSafe=false;
        if(mode==1)r.conditions.safetyTelemetryValid=false;
        if(mode==2)r.conditions.gameStateKnown=false;
        if(mode==3)r.conditions.gameActive=true;
        if(mode==4)r.conditions.drift=true;
        if(mode==5)r.conditions.fault=true;
        if(mode==6)r.permission=c.deadlineMonoMs-1;
        if(mode==7)r.permission=0;
        if(mode==8)r.intent.stop();
        if(mode==9)r.owner->transportFailed();
        if(mode==10)r.owner->requestStop();
        if(mode==11){auto skew=at(3);++skew.utcMs;r.owner->poll(skew,r.conditions,r.permission);}
        if(mode!=11)CHECK(!r.poll(3));
        CHECK_MSG(("experiment revoke "+std::to_string(mode)).c_str(),r.owner->snapshot().revokeRequired&&r.owner->snapshot().pending);
        CHECK(r.owner->acknowledge(receipt,at(4),r.conditions,r.permission)==A::Terminal);
        CHECK(!r.owner->snapshot().pending&&r.owner->snapshot().lastRun==original&&r.owner->snapshot().receipt->observedMonoMs==at(2).monotonicMs);
        r.conditions={true,true,true,false,false,false};r.permission=UINT64_MAX;r.intent.stop();r.intent.explicitStart();
        CHECK(!r.poll(5)&&!r.owner->start(r.sample(5),at(5),r.conditions,r.permission));
    }
    {Rig r;const auto c=r.send(1);r.permission=c.deadlineMonoMs;CHECK(!r.poll(56)&&r.owner->snapshot().state==S::Running&&r.owner->snapshot().pending);
        CHECK(!r.poll(60)&&r.owner->snapshot().revokeRequired);} // headroom cannot revoke a covered pending Run
    {Rig r;r.send(1);const auto old=*r.issuer.snapshot(at(1).monotonicMs).settled;
        CHECK(r.owner->acknowledge(old,at(2),r.conditions,r.permission)==A::Duplicate&&r.owner->snapshot().pending);}
    {Rig r;r.send(1);r.apply(2);auto s=r.sample(5);s.milliwatts.reset();CHECK(!r.owner->append(2,s,at(5),r.conditions,r.permission)&&r.owner->snapshot().revokeRequired);}
    {Rig r;CHECK(!r.poll(61)&&r.owner->snapshot().status==R::Coverage);}
    {Rig r(false);r.settings.maximumElapsedMs=120;CHECK(r.start());CHECK(r.owner->measurement().deadlineMonoMs==at(120).monotonicMs);
        r.send(1);r.apply(2);CHECK(r.add(5)&&r.add(15)&&r.add(35)&&r.add(45)&&r.add(55));const auto c=r.send(55);r.apply(56);
        CHECK(r.add(65));const auto change=r.send(65);CHECK(c.deadlineMonoMs==at(115).monotonicMs&&change.deadlineMonoMs==at(120).monotonicMs);r.apply(66);
        CHECK(r.add(70)&&r.add(80)&&r.add(100)&&r.add(110));CHECK(!r.poll(120)&&r.owner->snapshot().revokeRequired);}
    {Rig r(false);r.permission=at(40).monotonicMs;CHECK(r.start());CHECK(r.send(1).deadlineMonoMs==r.permission);}
    {Rig r;r.send(1);r.apply(2);r.owner.reset();CHECK(r.issuer.snapshot(at(3).monotonicMs).state==WorkerControlState::Stopped);}
}
void integrity(){
    for(unsigned mode=0;mode<4;++mode){Rig r;r.send(1);auto receipt=r.apply(2,false);
        if(mode==0)++receipt.command.revision;
        if(mode==1)++receipt.command.deadlineMonoMs;
        if(mode==2)receipt.command.hostWaitMs=10;
        if(mode==3)receipt.command.binding.workerKey[0]='a';
        CHECK(r.owner->acknowledge(receipt,at(3),r.conditions,r.permission)==A::Rejected&&r.owner->snapshot().revokeRequired);}
    {Rig r;CHECK(r.issuer.issue(D::Pause,at(1).monotonicMs,100).has_value());CHECK(!r.poll(1)&&r.owner->snapshot().status==R::Attribution);}
    {Rig r;r.send(1);auto receipt=r.apply(2,false);CHECK(r.issuer.acknowledge(receipt,at(2).monotonicMs)==A::Settled);
        CHECK(!r.poll(3)&&r.owner->snapshot().revokeRequired);} // external acknowledgement cannot steal the flight
    for(unsigned mode=0;mode<7;++mode){Rig r(false);
        if(mode==0)r.commands.enabled=false;
        if(mode==1)r.commands.commandLeaseMs=99;
        if(mode==2)r.commands.commandLeaseMs=5001;
        if(mode==3)r.commands.renewalMarginMs=0;
        if(mode==4)r.commands.renewalMarginMs=51;
        if(mode==5)r.commands.handoffTimeoutMs=0;
        if(mode==6)r.settings.qualification=WorkProfileQualification::Unsupported;
        CHECK(!r.start());CHECK(mode? r.owner->snapshot().revokeRequired:r.owner->snapshot().state==S::Disabled);}
    {Rig r(false);SessionIntent other;const auto t=other.explicitStart();WorkExperimentOwner owner(policy(),r.settings,binding(),r.issuer,other,t);
        CHECK(!owner.start(r.sample(0),at(),r.conditions,r.permission)&&owner.snapshot().revokeRequired);}
    {Rig r;r.send(1);CHECK(!r.poll(0)&&r.owner->snapshot().status==R::Clock);}
    {Rig r;auto skew=at(1);skew.utcMs+=2001;CHECK(!r.owner->poll(skew,r.conditions,r.permission)&&r.owner->snapshot().status==R::Clock);}
}
struct Economy {
    Rig &r;EconomicParentBudget budget;EconomicEvaluationOwner evaluator;EconomicCommandOwner owner;
    explicit Economy(Rig &f):r(f),budget({true,0,10000,2,4},f.intent,f.token),evaluator(evaluatorPolicy(),f.intent,f.token),
        owner(economicPolicy(),binding(),evaluator,f.issuer,budget,f.intent,f.token){CHECK(budget.start(opt())&&evaluator.start(opt()));}
    void study(uint64_t ms){r.now=at(ms);const auto ticket=evaluator.poll(opt(ms),1,r.settings,r.conditions).requests[1];CHECK(ticket.has_value());if(!ticket)return;
        const auto measured=buildWorkProfile(r.owner->snapshot().record->measurement->input);CHECK(measured!=nullptr);if(!measured)return;
        auto in=input(measured);in.generation=r.token.epoch;in.nowUtcMs=at(ms).utcMs;in.nowMonoMs=at(ms).monotonicMs;
        prices(in);in.electricity.opening.throughUtcMs=in.nowUtcMs+1;in.electricity.opening.timeDatabaseVersion=civilAt(in.electricity.plan.clock,in.nowUtcMs+1).databaseVersion;
        CHECK(evaluator.submit(*ticket,1,std::move(in),opt(ms))==EconomicSubmission::Accepted);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do{evaluator.poll(opt(ms),1,r.settings,r.conditions);if(!evaluator.snapshot().pending)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<until);
        CHECK(evaluator.snapshot().study&&!evaluator.snapshot().pending);
    }
    std::optional<WorkerCommand> poll(uint64_t ms){r.now=at(ms);evaluator.poll(opt(ms),1,r.settings,r.conditions);return owner.poll(opt(ms),1,r.settings,r.conditions,UINT64_MAX);}
    void apply(const WorkerCommand &command,uint64_t ms){r.now=at(ms);CHECK(r.gate.receive(command)==WorkerCommandResult::Accepted);r.gate.pauseBoundary(command.directive==D::Pause);
        if(command.directive==D::Run)CHECK(r.gate.applyWorkWait(r.installed)==WorkControlRequest::Run);
        CHECK(owner.acknowledge(*r.gate.snapshot().receipt,opt(ms))==A::Settled);}
};
void handoff(){
    {Rig r;Economy e(r);CHECK(r.complete());e.study(136);CHECK(e.owner.startAfterMeasurement(opt(136),r.settings,*r.owner));
        const auto original=r.owner->snapshot().record;CHECK(r.owner->snapshot().state==S::Transferred&&e.owner.snapshot().measurement==original);
        CHECK(e.budget.snapshot().incarnations==1&&e.budget.snapshot().attached&&e.budget.snapshot().transitionsInWindow==0);
        const auto waiting=e.poll(136);CHECK((!waiting||waiting->directive==D::Pause)&&!e.owner.snapshot().lastRun);
        if(waiting)e.apply(*waiting,137); // same-millisecond evaluation cannot cross the barrier; a safe Pause renewal is allowed
        r.owner->requestStop();r.owner->transportFailed();CHECK(!r.poll(138)&&r.issuer.snapshot(at(138).monotonicMs).state==WorkerControlState::Active);
        CHECK(!r.owner->append(99,r.sample(138),at(138),r.conditions,UINT64_MAX));
        const auto pause=e.poll(1040);CHECK(pause&&pause->directive==D::Pause);if(pause)e.apply(*pause,1041);
        e.study(1136);const auto run=e.poll(1137);CHECK(run&&run->directive==D::Run);if(run)e.apply(*run,1138);
        e.poll(1138);CHECK(e.budget.snapshot().transitionsInWindow==1&&e.owner.snapshot().application&&e.owner.snapshot().measurement==original);
        CHECK(!e.owner.startAfterMeasurement(opt(1138),r.settings,*r.owner));
        r.issuer.requestStop();CHECK(e.owner.retire(opt(1139))&&e.budget.snapshot().transitionsInWindow==1&&!e.budget.snapshot().attached);
    }
    {Rig r;Economy e(r);CHECK(r.complete());CHECK(!e.owner.start(opt(136),r.settings));} // an arbitrary noninitial Pause is still refused
    for(unsigned mode=0;mode<6;++mode){Rig r;Economy e(r);CHECK(r.complete());auto settings=r.settings;uint64_t ms=136;
        if(mode==0)++settings.validForMs;
        if(mode==1)r.owner->requestStop();
        if(mode==2)r.intent.stop();
        if(mode==3){r.send(226);ms=226;}
        if(mode==4){ms=635;}
        if(mode==5){CHECK(r.issuer.issue(D::Pause,at(136).monotonicMs,100).has_value());}
        CHECK(!e.owner.startAfterMeasurement(opt(ms),settings,*r.owner)&&e.owner.snapshot().revokeRequired);
    }
    {Rig r;CHECK(r.complete());r.send(226);r.apply(227);r.send(321);r.apply(322);r.send(416);r.apply(417);r.send(511);r.apply(512);r.send(606);r.apply(607);
        CHECK(!r.poll(635)&&r.owner->snapshot().revokeRequired&&r.owner->snapshot().record);} // Pause heartbeats never extend handoff
    {Rig r;Economy e(r);CHECK(r.complete());CHECK(e.owner.startAfterMeasurement(opt(136),r.settings,*r.owner));
        EconomicCommandOwner duplicate(economicPolicy(),binding(),e.evaluator,r.issuer,e.budget,r.intent,r.token);
        CHECK(!duplicate.startAfterMeasurement(opt(137),r.settings,*r.owner)&&duplicate.snapshot().revokeRequired);
        CHECK(e.budget.snapshot().attached&&e.budget.snapshot().incarnations==1&&!e.budget.start(opt(137)));}
    {Rig r;Economy e(r);CHECK(r.complete());auto wrong=binding();wrong.workerKey=std::string(32,'3');
        EconomicCommandOwner other(economicPolicy(),wrong,e.evaluator,r.issuer,e.budget,r.intent,r.token);
        CHECK(!other.startAfterMeasurement(opt(136),r.settings,*r.owner)&&other.snapshot().revokeRequired&&r.owner->snapshot().record);
        CHECK(r.issuer.snapshot(at(136).monotonicMs).state==WorkerControlState::Stopped&&!e.budget.snapshot().attached);}
    {Rig r;Economy e(r);CHECK(r.complete());WorkerControlIssuer other(binding(),r.intent,r.token);
        CHECK(other.start(at(136).monotonicMs));const auto pause=other.issue(D::Pause,at(136).monotonicMs,5000);CHECK(pause.has_value());
        if(pause)CHECK(other.acknowledge({*pause,K::Paused,at(136).monotonicMs},at(136).monotonicMs)==A::Settled);
        EconomicCommandOwner replacement(economicPolicy(),binding(),e.evaluator,other,e.budget,r.intent,r.token);
        CHECK(!replacement.startAfterMeasurement(opt(136),r.settings,*r.owner)&&r.owner->snapshot().revokeRequired);}
    {Rig r(false);r.commands.handoffTimeoutMs=5000;CHECK(r.start());Economy e(r);CHECK(r.complete());
        auto transfer=opt(136);transfer.utcMs+=1500;CHECK(e.owner.startAfterMeasurement(transfer,r.settings,*r.owner));
        auto skewed=opt(137);skewed.utcMs+=2001;
        CHECK(!e.owner.poll(skewed,1,r.settings,r.conditions,UINT64_MAX)&&e.owner.snapshot().state==EconomicCommandState::ClockFault);}
}
}
int main(){fullExperiment();constraintsAndLate();integrity();handoff();return finishTests("owned work experiment commands and handoff");}
