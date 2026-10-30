// Actual issuer/gate with explicit synthetic original observations; no native process/device/provider.
#include "economic_fixture.h"
#include "product/work_measurement.h"
#include "product_test_util.h"
using namespace economic_fixture;
namespace {
using S=WorkMeasurementState;using R=WorkProfileStatus;using D=WorkerDirective;using K=WorkerReceiptKind;
WorkMeasurementTime at(uint64_t ms=0){return {T+static_cast<int64_t>(ms),10000+ms};}
WorkerControlBinding binding(){return {std::string(32,'1'),std::string(32,'2'),1,1,0};}
WorkProfilePlan plan(){auto p=profile()->input().plan;p.telemetryCadenceMs=5;p.maximumSampleGapMs=50;p.warmupMs=10;p.measurementMs=20;
    p.repetitions=2;p.maximumElapsedMs=1000;p.maximumEnergy.numerator=d("1000000");return p;}
struct Rig {
    SessionIntent intent;SessionToken token=intent.explicitStart();WorkProfilePlan policy=plan();WorkMeasurementTime now=at();
    WorkerControlIssuer issuer{binding(),intent,token};WorkerControlGate gate{binding(),[&]{return now.monotonicMs;}};
    WorkProfileConditions conditions{true,true,true,false,false,false};unsigned installed=0;uint64_t sequence=1;
    std::unique_ptr<WorkMeasurementOwner> owner;
    explicit Rig(bool begin=true){CHECK(issuer.start(now.monotonicMs));const auto c=issuer.issue(D::Pause,now.monotonicMs,5000);
        CHECK(c&&gate.receive(*c)==WorkerCommandResult::Accepted);gate.pauseBoundary(true);
        CHECK(issuer.acknowledge(*gate.snapshot().receipt,now.monotonicMs)==WorkerReceiptResult::Settled);
        if(begin)CHECK(start());
    }
    WorkProfileSample sample(uint64_t ms)const{WorkProfileSample s;s.generation=token.epoch;s.counterEpoch=1;s.fingerprint=workProfileFingerprint(policy.identity);
        s.utcMs=at(ms).utcMs;s.monotonicMs=at(ms).monotonicMs;s.appliedHostWaitMs=installed;s.completeWorkBoundary=s.safetyTelemetryValid=s.thermalSafe=s.gameStateKnown=true;
        s.milliwatts=installed?100000:200000;s.userTmacs=d(std::to_string(ms*2));s.donationTmacs=d(std::to_string(ms));return s;}
    bool start(){owner=std::make_unique<WorkMeasurementOwner>(policy,binding(),issuer,intent,token);return owner->start(sample(0),now,conditions);}
    WorkerCommand send(uint64_t ms,D directive,std::optional<unsigned> wait={}){now=at(ms);const auto c=issuer.issue(directive,now.monotonicMs,5000,wait);
        CHECK(c&&gate.receive(*c)==WorkerCommandResult::Accepted);CHECK(issuer.acknowledge(*gate.snapshot().receipt,now.monotonicMs)==WorkerReceiptResult::Waiting);return c?*c:WorkerCommand{};}
    WorkerReceipt applied(uint64_t ms){now=at(ms);const auto pending=*issuer.snapshot(now.monotonicMs).pending;
        gate.pauseBoundary(pending.directive==D::Pause);if(pending.directive==D::Run)CHECK(gate.applyWorkWait(installed)==WorkControlRequest::Run);
        const auto r=*gate.snapshot().receipt;CHECK(issuer.acknowledge(r,now.monotonicMs)==WorkerReceiptResult::Settled);return r;}
    bool add(uint64_t ms,bool boundary=true){now=at(ms);auto s=sample(ms);s.completeWorkBoundary=boundary;return owner->append(++sequence,std::move(s),now,conditions);}
    const WorkMeasurementSnapshot &poll(uint64_t ms){now=at(ms);return owner->poll(now,conditions);}
};
void fullRecord(){Rig r;CHECK(r.owner->snapshot().requestedHostWaitMs==0&&r.owner->snapshot().sampleDueMonoMs==at(5).monotonicMs);
    r.send(1,D::Run,0);CHECK(r.poll(1).state==S::AwaitingApplication&&r.owner->snapshot().completedWindows==0);
    r.applied(3);CHECK(r.add(5)&&r.owner->snapshot().state==S::Warming);
    CHECK(r.add(15)&&r.owner->snapshot().state==S::Measuring);CHECK(r.add(35)&&r.owner->snapshot().completedWindows==1&&r.owner->snapshot().state==S::Warming);
    // Same-mode heartbeat does not restart warm-up or spend another measurement window.
    r.send(36,D::Run,0);r.applied(37);CHECK(r.add(45)&&r.owner->snapshot().state==S::Measuring);
    CHECK(r.add(65)&&r.owner->snapshot().requestedHostWaitMs==10&&r.owner->snapshot().state==S::AwaitingApplication);
    CHECK(r.add(70));r.send(71,D::Run,10);r.applied(72);CHECK(r.add(75));CHECK(r.add(85));CHECK(r.add(105));CHECK(r.add(115));
    CHECK(r.add(135)&&r.owner->snapshot().state==S::Draining&&!r.owner->snapshot().requestedHostWaitMs&&!r.owner->snapshot().record);
    r.send(136,D::Pause);CHECK(r.poll(136).state==S::Draining&&!r.owner->snapshot().record);r.applied(137);
    CHECK(r.add(140)&&r.owner->snapshot().state==S::Complete&&!r.owner->snapshot().revokeRequired);
    const auto original=r.owner->snapshot().record;R status;const auto p=buildWorkProfile(original->input,&status);
    CHECK(p&&status==R::Ok&&original->input.windows.size()==4&&original->input.samples.size()==13&&original->input.generation==r.token.epoch);
    CHECK(original->receipts.size()==5&&original->receipts.front().kind==K::Paused&&original->receipts.back().kind==K::Paused&&
        original->receipts[2].observedMonoMs==at(37).monotonicMs&&original->binding.workerKey==binding().workerKey&&
        original->admitted.monotonicMs==at().monotonicMs&&original->completed.monotonicMs==at(140).monotonicMs);
    if(p){const auto &s=p->summary();CHECK(s.elapsedMs==140&&s.measuredMs==80&&s.overheadMs==60&&s.completeCoverage);
        CHECK(s.totalUserTmacs.text()=="280"&&s.totalDonationTmacs.text()=="140"&&s.totalEnergy.numerator.text()=="21500");
        CHECK(s.points[0].measuredMs==40&&s.points[1].measuredMs==40&&s.measuredEnergy.numerator.text()=="12000"&&s.overheadEnergy.numerator.text()=="9500");}
    CHECK(r.owner->snapshot().observedEnergy.numerator.text()=="21500"&&!r.owner->snapshot().sampleDueMonoMs);
    r.owner->requestStop();CHECK(r.owner->snapshot().state==S::Stopped&&r.owner->snapshot().record==original&&r.owner->snapshot().revokeRequired);
    r.intent.stop();r.intent.explicitStart();CHECK(r.poll(150).state==S::Stopped&&!r.owner->start(r.sample(150),at(150),r.conditions));
}
void originalWindows(){
    {Rig r(false);r.policy.warmupMs=0;r.policy.repetitions=1;r.policy.hostWaitMs={0};CHECK(r.start());r.send(1,D::Run,0);r.applied(2);
        CHECK(r.add(5)&&r.owner->snapshot().state==S::Measuring);CHECK(r.add(24,false));CHECK(r.add(30)&&r.owner->snapshot().state==S::Draining);
        r.send(31,D::Pause);r.applied(32);CHECK(r.add(35));const auto in=r.owner->snapshot().record;CHECK(in&&in->input.windows[0].beginSample==in->input.windows[0].measurementSample);
        const auto p=buildWorkProfile(in->input);CHECK(p&&p->summary().measuredMs==25&&p->summary().overheadMs==10);}
    {Rig r;r.send(1,D::Run,0);r.applied(2);CHECK(r.add(5));CHECK(r.add(20,false));CHECK(r.owner->snapshot().state==S::Warming);
        CHECK(r.add(25)&&r.owner->snapshot().state==S::Measuring);CHECK(r.add(45)&&r.owner->snapshot().completedWindows==1);}
    {Rig r;r.send(1,D::Run,0);r.applied(2);CHECK(r.add(5));CHECK(r.add(50,false));CHECK(!r.add(66,false)&&r.owner->snapshot().status==R::Coverage);}
    {Rig r;r.send(1,D::Run,0);r.applied(2);CHECK(r.add(5));CHECK(r.add(15));CHECK(r.add(60,false));CHECK(!r.add(86,false)&&r.owner->snapshot().status==R::Coverage);}
}
void rejectedSources(){
    for(unsigned mode=0;mode<17;++mode){Rig r;r.send(1,D::Run,0);r.applied(2);CHECK(r.add(5));auto s=r.sample(10);auto time=at(10);auto seq=r.sequence+1;
        if(mode==0)++s.generation;
        if(mode==1)++s.counterEpoch;
        if(mode==2)s.fingerprint="fixture.other";
        if(mode==3)s.milliwatts.reset();
        if(mode==4)s.userTmacs.reset();
        if(mode==5)s.donationTmacs.reset();
        if(mode==6)s.userTmacs=d("0");
        if(mode==7)s.donationTmacs=d("0");
        if(mode==8)s.thermalSafe=false;
        if(mode==9)s.gameActive=true;
        if(mode==10)++s.utcMs;
        if(mode==11)s.appliedHostWaitMs=10;
        if(mode==12)++seq;
        if(mode==13)--seq;
        if(mode==14)s.userTmacs=d("-1");
        if(mode==15){s=r.sample(60);time=at(60);}
        if(mode==16){s.monotonicMs=at(11).monotonicMs;}
        CHECK_MSG(("source refusal "+std::to_string(mode)).c_str(),!r.owner->append(seq,s,time,r.conditions));
        CHECK(r.owner->snapshot().state==S::Faulted&&r.owner->snapshot().revokeRequired&&!r.owner->snapshot().record&&r.owner->snapshot().sampleCount==2);
        CHECK(r.poll(65).state==S::Faulted&&!r.add(70));
    }
    {Rig r;r.send(1,D::Run,0);r.applied(10);const auto old=r.sample(5);CHECK(!r.owner->append(2,old,at(11),r.conditions)&&r.owner->snapshot().status==R::Attribution);}
}
void controlAndStop(){
    for(unsigned mode=0;mode<8;++mode){Rig r;r.send(1,D::Run,0);r.applied(2);CHECK(r.add(5));
        if(mode==0)r.conditions.safetyTelemetryValid=false;
        if(mode==1)r.conditions.thermalSafe=false;
        if(mode==2)r.conditions.gameStateKnown=false;
        if(mode==3)r.conditions.gameActive=true;
        if(mode==4)r.conditions.fault=true;
        if(mode==5)r.conditions.drift=true;
        if(mode==6)r.intent.stop();
        if(mode==7)r.owner->requestStop();
        CHECK(!r.add(10)&&r.owner->snapshot().revokeRequired&&!r.owner->snapshot().record);
        r.conditions={true,true,true,false,false,false};CHECK(!r.add(15));}
    {Rig r;r.send(1,D::Run,10);CHECK(r.poll(1).status==R::Attribution&&r.owner->snapshot().revokeRequired);}
    {Rig r;r.send(1,D::Run,0);r.applied(2);CHECK(r.add(5));r.send(6,D::Pause);CHECK(r.poll(6).status==R::Attribution);}
    {Rig r;r.send(1,D::Run,0);r.applied(2);r.send(3,D::Run,0);r.applied(4);CHECK(r.poll(4).status==R::Attribution);} // skipped receipt
    {Rig r;CHECK(r.poll(51).status==R::Coverage&&r.owner->snapshot().revokeRequired);}
    {Rig r;CHECK(r.poll(5).state==S::AwaitingApplication);CHECK(r.poll(4).status==R::Clock);}
    {Rig r;auto skewed=at(1);skewed.utcMs+=2001;CHECK(r.owner->poll(skewed,r.conditions).status==R::Clock);}
    {Rig r;auto c=r.send(1,D::Run,0);r.applied(2);auto owner=std::move(r.owner);owner.reset();CHECK(r.issuer.snapshot(c.issuedMonoMs+2).state==WorkerControlState::Stopped);}
}
void budgetsAndAdmission(){
    {Rig r(false);r.policy.maximumElapsedMs=120;CHECK(r.start());CHECK(r.poll(121).status==R::Budget);}
    {Rig r(false);r.policy.maximumEnergy.numerator=d("1999");CHECK(r.start());r.send(1,D::Run,0);r.applied(2);
        CHECK(!r.add(10)&&r.owner->snapshot().status==R::Budget&&r.owner->snapshot().observedEnergy.numerator.zero());}
    {Rig r(false);r.policy.maximumEnergy.numerator=d("2000");CHECK(r.start());r.send(1,D::Run,0);r.applied(2);
        CHECK(r.add(10)&&r.owner->snapshot().observedEnergy.numerator.text()=="2000");CHECK(!r.add(11)&&r.owner->snapshot().status==R::Budget);}
    for(unsigned mode=0;mode<4;++mode){Rig r(false);
        if(mode==0)r.policy.enabled=false;
        if(mode==1)r.policy.qualification=WorkProfileQualification::Unsupported;
        if(mode==2)r.policy.maximumElapsedMs=119;
        if(mode==3)r.policy.identity.deviceKey.clear();
        CHECK(!r.start()&&r.owner->snapshot().revokeRequired);}
    {Rig r(false);SessionIntent other;const auto token=other.explicitStart();WorkMeasurementOwner owner(r.policy,binding(),r.issuer,other,token);
        CHECK(!owner.start(r.sample(0),at(),r.conditions)&&owner.snapshot().status==R::Attribution);}
    {Rig r(false);auto wrong=binding();wrong.workerKey[0]='a';WorkMeasurementOwner owner(r.policy,wrong,r.issuer,r.intent,r.token);
        CHECK(!owner.start(r.sample(0),at(),r.conditions));}
    {Rig r(false);WorkMeasurementOwner owner(r.policy,binding(),r.issuer,r.intent,r.token);owner.requestStop();CHECK(!owner.start(r.sample(0),at(),r.conditions));}
}
void zeroAndCapacity(){
    {Rig r(false);r.policy.warmupMs=0;r.policy.measurementMs=10;r.policy.repetitions=1;r.policy.hostWaitMs={0};
        r.owner=std::make_unique<WorkMeasurementOwner>(r.policy,binding(),r.issuer,r.intent,r.token);
        auto zero=[&](uint64_t ms){auto s=r.sample(ms);s.milliwatts=0;s.userTmacs=s.donationTmacs=d("0");return s;};
        CHECK(r.owner->start(zero(0),at(),r.conditions));r.send(1,D::Run,0);r.applied(2);
        CHECK(r.owner->append(2,zero(5),at(5),r.conditions));CHECK(r.owner->append(3,zero(15),at(15),r.conditions));
        r.send(16,D::Pause);r.applied(17);CHECK(r.owner->append(4,zero(20),at(20),r.conditions));
        const auto record=r.owner->snapshot().record;const auto p=record?buildWorkProfile(record->input):nullptr;
        CHECK(p&&p->summary().completeCoverage&&p->summary().totalEnergy.numerator.zero()&&p->summary().totalUserTmacs.zero()&&p->summary().totalDonationTmacs.zero());}
    {Rig r(false);r.policy.telemetryCadenceMs=1;r.policy.warmupMs=0;r.policy.measurementMs=10000;r.policy.maximumElapsedMs=20000;
        r.policy.repetitions=1;r.policy.hostWaitMs={0};r.policy.maximumEnergy.numerator=d("1000000000");CHECK(r.start());r.send(0,D::Run,0);r.applied(0);
        bool accepted=true;for(uint64_t i=1;i<8192;++i){if(i%4000==0){r.send(i,D::Run,0);r.applied(i);}if(!r.add(i,i==1)){accepted=false;break;}}
        CHECK(accepted&&r.owner->snapshot().sampleCount==8192&&r.owner->snapshot().state==S::Measuring);
        CHECK(!r.add(8192)&&r.owner->snapshot().status==R::Capacity&&r.owner->snapshot().sampleCount==8192&&!r.owner->snapshot().record);}
    {Rig r;bool admitted=true;
        for(unsigned i=1;i<8192;++i){r.send(0,D::Pause);r.applied(0);if(r.poll(0).state!=S::AwaitingApplication){admitted=false;break;}}
        CHECK(admitted);r.send(0,D::Pause);r.applied(0);CHECK(r.poll(0).status==R::Capacity&&!r.owner->snapshot().record);}
    {Rig r(false);r.policy.maximumSampleGapMs=60000;CHECK(r.start());CHECK(r.poll(5000).status==R::Unsafe&&r.owner->snapshot().revokeRequired);}
}
void transitionFlights(){
    for(bool final:{false,true}){Rig r(false);r.policy.warmupMs=0;r.policy.repetitions=1;
        if(final)r.policy.hostWaitMs={0};
        CHECK(r.start());r.send(1,D::Run,0);r.applied(2);CHECK(r.add(5));
        // Exact ordering, even within one millisecond: this renewal was pending BEFORE the boundary.
        r.send(25,D::Run,0);CHECK(r.add(25));
        const auto expected=final?S::Draining:S::AwaitingApplication;
        CHECK_MSG("original same-wait flight survives the request transition",r.poll(25).state==expected&&!r.owner->snapshot().revokeRequired);
        if(r.owner->snapshot().revokeRequired)continue;
        r.applied(26);CHECK(r.poll(26).state==expected);CHECK(r.add(30));
        CHECK(r.owner->snapshot().completedWindows==1&&!r.owner->snapshot().record);
        if(final){r.send(31,D::Pause);r.applied(32);CHECK(r.add(35)&&r.owner->snapshot().record);}
        else{r.send(31,D::Run,10);r.applied(32);CHECK(r.add(35)&&r.owner->snapshot().state==S::Measuring);}
    }
    for(bool final:{false,true}){Rig r(false);r.policy.warmupMs=0;r.policy.repetitions=1;
        if(final)r.policy.hostWaitMs={0};
        CHECK(r.start());r.send(1,D::Run,0);r.applied(2);CHECK(r.add(5));CHECK(r.add(25));
        // Same timestamp is not evidence this command preceded the request transition.
        r.send(25,D::Run,0);CHECK(r.poll(25).status==R::Attribution&&r.owner->snapshot().revokeRequired);
    }
    {Rig r;auto skew=at(1);skew.utcMs+=10;r.owner->poll(skew,r.conditions);
        CHECK(r.owner->snapshot().sourceValidUntilMonoMs==at(40).monotonicMs&&r.owner->snapshot().latestSample->monotonicMs==at().monotonicMs);
        CHECK(r.poll(20).sourceValidUntilMonoMs==at(40).monotonicMs); // cached source cannot grow when clock disagreement shrinks
        CHECK(r.add(21)&&r.owner->snapshot().sourceValidUntilMonoMs==at(71).monotonicMs&&r.owner->snapshot().requestRevision==1);}
}
}
int main(){fullRecord();originalWindows();rejectedSources();controlAndStop();budgetsAndAdmission();zeroAndCapacity();transitionFlights();return finishTests("receipt-bound work measurement capture");}
