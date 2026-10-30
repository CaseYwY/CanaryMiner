// Real raw accumulator and pause helper, synthetic clocks/power support. No hardware/source qualification.
#include "product/work_power_alignment.h"
#include "core/work_pause.h"
#include "product_test_util.h"
using namespace om;
namespace {
using K=CompletedWorkBoundaryKind;using R=WorkProfileStatus;using S=CompletedWorkState;
const std::string key(32,'1');
CompletedWorkTime at(uint64_t ms=0){return {1700000000000LL+static_cast<int64_t>(ms),10000+ms};}
struct Rig {
    SessionIntent intent;SessionToken token=intent.explicitStart();CompletedWorkCapture work{{true,8192,key},intent,token,5};
    Rig(){CHECK(work.start(at())&&work.begin(WorkLane::User,at()));}
    bool boundary(uint64_t n,K k=K::Paused,unsigned wait=0){return work.boundary({at(n),k,wait});}
    CompletedWorkInterval interval(){CHECK(boundary(1)&&boundary(21));CHECK(work.snapshot().lastInterval.has_value());return *work.snapshot().lastInterval;}
};
WorkPowerAlignmentPolicy policy(){return {true,key,1,5,{PowerBasis::GpuBoard,PowerOrigin::SyntheticFixture,"fixture.board","fixture.sensor"},100,20,100};}
WorkPowerReading reading(){WorkPowerReading r;r.workSourceKey=key;r.generation=1;r.counterEpoch=5;r.sequence=1;r.evidence=policy().evidence;
    r.observed=at(10);r.readBegin=at(9);r.readEnd=at(12);r.support=WorkPowerSupport{at(8),at(10)};r.milliwatts=123000;return r;}
void intervals(){
    Rig r;const auto first=r.interval();CHECK(first.begin.monotonicMs==at(1).monotonicMs&&first.end.monotonicMs==at(21).monotonicMs&&
        first.userTmacs.zero()&&first.donationTmacs.zero()&&first.run==1&&first.number==1&&first.batches==0&&first.beginEvent==1&&first.endEvent==2);
    CHECK(r.boundary(25,K::Leaving)&&r.work.completed({at(40),6,2,524288,10,false}));
    const auto batch=r.work.snapshot().lastBatch;CHECK(r.boundary(41,K::Runnable,10)&&r.boundary(61,K::Leaving,10));
    auto next=*r.work.snapshot().lastInterval;CHECK(next.number==3&&next.kind==K::Runnable&&next.appliedHostWaitMs==10&&next.batches==1&&
        next.userTmacs.text()=="0.000003145728"&&next.donationTmacs.zero()&&next.begin.monotonicMs==at(41).monotonicMs);
    CHECK(r.boundary(62,K::Runnable,0)&&r.boundary(82,K::Leaving,0)); // explicit break before a changed wait, including zero
    CHECK(r.work.end(true,at(83))&&r.work.begin(WorkLane::Donation,at(90))&&r.boundary(91)&&r.boundary(111,K::Leaving));
    next=*r.work.snapshot().lastInterval;CHECK(next.run==2&&next.lane==WorkLane::Donation&&next.begin.monotonicMs==at(91).monotonicMs);
    CHECK(first.run==1&&first.lane==WorkLane::User&&first.end.monotonicMs==at(21).monotonicMs&&first.userTmacs.zero());
    CHECK(r.work.snapshot().lastBatch->batch.observed.monotonicMs==batch->batch.observed.monotonicMs&&r.work.snapshot().lastBatch->lane==WorkLane::User);
    CHECK(r.work.end(true,at(120)));const auto retained=*r.work.snapshot().lastInterval;
    r.work.requestStop();CHECK(!r.boundary(150)&&r.work.snapshot().state==S::Stopped&&r.work.snapshot().lastInterval->end.monotonicMs==retained.end.monotonicMs);
    {Rig q;CHECK(q.boundary(1)&&q.boundary(1)&&!q.work.snapshot().lastInterval);CHECK(q.work.end(true,at(100))&&!q.work.snapshot().lastInterval);}
    for(unsigned mode=0;mode<7;++mode){Rig q;CHECK(q.boundary(1));bool result=false;
        if(mode==0)result=q.boundary(2,K::Runnable);
        if(mode==1)result=q.boundary(2,K::Paused,1);
        if(mode==2)result=q.boundary(2,K::Leaving,1);
        if(mode==3)result=q.work.completed({at(2),6,2,524288,0,false});
        if(mode==4)result=q.boundary(2,static_cast<K>(90));
        if(mode==5)result=q.boundary(0);
        if(mode==6)result=q.boundary(2,K::Paused,1001);
        CHECK(!result&&q.work.snapshot().state==S::Faulted&&!q.work.snapshot().lastInterval);}
    {SessionIntent i;const auto t=i.explicitStart();CompletedWorkCapture bad({true,1,"not-a-key"},i,t,1);CHECK(!bad.start(at()));}
}
void originals(){Rig r;auto interval=r.interval();auto power=reading();R status;const auto expected=policy();
    auto aligned=alignWorkPower(expected,interval,power,at(30),&status);CHECK(aligned&&status==R::Ok);
    power.milliwatts=9;interval.end=at(31);
    CHECK(aligned&&aligned->power.milliwatts==123000&&aligned->power.observed.monotonicMs==at(10).monotonicMs&&
        aligned->power.readEnd.monotonicMs==at(12).monotonicMs&&aligned->power.support->begin.monotonicMs==at(8).monotonicMs&&
        aligned->work.end.monotonicMs==at(21).monotonicMs);
    power=reading();power.milliwatts=0;CHECK(alignWorkPower(expected,r.work.snapshot().lastInterval.value(),power,at(110),&status)&&status==R::Ok);
    CHECK(!alignWorkPower(expected,r.work.snapshot().lastInterval.value(),power,at(111),&status)&&status==R::Stale);
    // Observed telemetry may retain raw alignment, but cannot acquire SyntheticFixture profile eligibility.
    auto p=expected;p.evidence.origin=power.evidence.origin=PowerOrigin::ObservedTelemetry;
    CHECK(alignWorkPower(p,r.work.snapshot().lastInterval.value(),power,at(30),&status)&&status==R::Ok);
    WorkProfilePlan plan;plan.enabled=true;plan.qualification=WorkProfileQualification::SyntheticFixture;plan.identity.power=p.evidence;
    CHECK(validateWorkProfilePlan(plan)==R::Unsupported);
}
void refusal(){
    for(unsigned mode=0;mode<36;++mode){Rig r;auto w=r.interval();auto s=reading();auto p=policy();auto now=at(30);R status=R::Ok;
        if(mode==0)p.enabled=false;
        if(mode==1)p.workSourceKey="bad";
        if(mode==2)p.generation=0;
        if(mode==3)p.counterEpoch=0;
        if(mode==4)p.maximumBracketMs=0;
        if(mode==5)p.maximumReadMs=60001;
        if(mode==6)p.maximumSourceAgeMs=60001;
        if(mode==7)p.evidence.adapter.clear();
        if(mode==8)p.evidence.origin=PowerOrigin::DeclaredForecast;
        if(mode==9)w.sourceKey=std::string(32,'2');
        if(mode==10)s.workSourceKey=std::string(32,'2');
        if(mode==11)++w.generation;
        if(mode==12)++s.generation;
        if(mode==13)++w.counterEpoch;
        if(mode==14)++s.counterEpoch;
        if(mode==15)s.evidence.scope="fixture.other";
        if(mode==16)w.run=0;
        if(mode==17)++w.endEvent;
        if(mode==18)s.sequence=0;
        if(mode==19)w.appliedHostWaitMs=1001;
        if(mode==20)w.kind=K::Leaving;
        if(mode==21)w.lane=static_cast<WorkLane>(9);
        if(mode==22)ExactDecimal::parse("-1",&w.userTmacs);
        if(mode==23)s.milliwatts.reset();
        if(mode==24)s.support.reset();
        if(mode==25)s.readBegin.utcMs=-1;
        if(mode==26)s.readEnd.monotonicMs=0;
        if(mode==27)++w.end.utcMs;
        if(mode==28)s.support->end=at(7);
        if(mode==29)s.support->end=at(15); // sensor support cannot extend beyond original read completion
        if(mode==30)now=at(20);
        if(mode==31)++s.observed.utcMs;
        if(mode==32)p.maximumBracketMs=19;
        if(mode==33)p.maximumReadMs=2;
        if(mode==34)now=at(111);
        if(mode==35)s.support->begin=at(0); // cached source spanning work is not boundary-aligned
        CHECK(!alignWorkPower(p,w,s,now,&status)&&status!=R::Ok);
    }
    // Every read/support endpoint must be strictly inside both original work clocks; same-ms ties are not guessed.
    for(unsigned edge=0;edge<2;++edge)for(unsigned field=0;field<5;++field){Rig r;auto w=r.interval();auto s=reading();R status=R::Ok;
        const auto t=edge?w.end:w.begin;
        if(field==0)s.observed=t;
        if(field==1)s.readBegin=t;
        if(field==2)s.readEnd=t;
        if(field==3)s.support->begin=t;
        if(field==4)s.support->end=t;
        CHECK(!alignWorkPower(policy(),w,s,at(30),&status));
    }
}
struct Control:RunPauseControl {
    bool held=false,run=false;unsigned pulses=0;
    bool pauseRequested()const noexcept override{return !run;}
    void pauseBoundary(bool p)noexcept override{held=p;}
    void pauseHeld()noexcept override{++pulses;}
};
void pauseOrdering(){
    Rig r;Control control;uint64_t now=1;bool drained=false,ordered=true;unsigned sleeps=0;std::optional<CompletedWorkInterval> interval;
    const auto result=pauseAtWorkBoundary(control,[]{return false;},[&]{drained=true;return true;},[](bool){},[&]{return now;},[&](unsigned ms){
        now+=ms;if(++sleeps==2)control.run=true;},[&](bool held){ordered=ordered&&drained&&control.held;
        const bool ok=r.boundary(now,held?K::Paused:K::Leaving);if(r.work.snapshot().lastInterval)interval=r.work.snapshot().lastInterval;return ok;});
    CHECK(result==WorkPauseResult::Ready&&ordered&&control.pulses==2&&!control.held&&interval&&interval->kind==K::Paused);
    auto p=reading();p.observed=at(15);p.support=WorkPowerSupport{at(14),at(15)};p.readBegin=at(14);p.readEnd=at(17);R status;
    CHECK(interval&&alignWorkPower(policy(),*interval,p,at(now),&status));
    for(unsigned mode=0;mode<3;++mode){Control c;bool drainedOnce=false;unsigned calls=0;
        const auto outcome=pauseAtWorkBoundary(c,[]{return false;},[&]{drainedOnce=true;return mode!=0;},[](bool){},[]{return 1;},
            [&](unsigned){c.run=true;},[&](bool held){++calls;CHECK(drainedOnce&&c.held);return mode==2&&held;});
        CHECK(outcome==WorkPauseResult::Failed&&calls==(mode==0?0u:mode==1?1u:3u)&&c.held==(mode!=0));
    }
}
}
int main(){intervals();originals();refusal();pauseOrdering();return finishTests("original power/work boundary alignment");}
