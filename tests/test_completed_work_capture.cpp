// Synthetic exact batch originals only. No run loop, device, transport, power or source qualification.
#include "product/completed_work_capture.h"
#include "product_test_util.h"
using namespace om;
namespace {
using S=CompletedWorkState;using R=CompletedWorkStatus;
CompletedWorkTime at(uint64_t n=0){return {1700000000000LL+static_cast<int64_t>(n),10000+n};}
CompletedWorkBatch batch(uint64_t n=1){return {at(n),6,2,524288,10,false};}
struct Rig {
    SessionIntent intent;SessionToken token=intent.explicitStart();CompletedWorkCapture source{{true,8192},intent,token,1};
    Rig(){CHECK(source.start(at()));}
};
void originals(){Rig r;CHECK(r.source.begin(WorkLane::User,at()));CHECK(r.source.completed(batch()));
    const auto original=r.source.snapshot().lastBatch;CHECK(original&&original->run==1&&original->number==1&&original->lane==WorkLane::User);
    CHECK(r.source.snapshot().userTmacs.text()=="0.000003145728"&&r.source.snapshot().donationTmacs.zero());
    CHECK(r.source.end(true,at(2))&&r.source.begin(WorkLane::Donation,at(3)));
    CHECK(r.source.snapshot().lastBatch->lane==WorkLane::User&&r.source.snapshot().lastBatch->batch.observed.monotonicMs==at(1).monotonicMs);
    CHECK(r.source.completed(batch(4))&&r.source.end(true,at(5))&&r.source.begin(WorkLane::User,at(6))&&r.source.completed(batch(7))&&r.source.end(true,at(8)));
    const auto &s=r.source.snapshot();CHECK(s.state==S::Ready&&s.runs==3&&s.batches==3&&s.attempts==6&&s.generation==r.token.epoch&&s.counterEpoch==1);
    CHECK(s.userTmacs.text()=="0.000006291456"&&s.donationTmacs.text()=="0.000003145728"&&s.workUnitsPerAttempt==3&&s.macsPerUnit==524288);
    CHECK(original->lane==WorkLane::User&&original->batch.observed.monotonicMs==at(1).monotonicMs&&original->batch.appliedHostWaitMs==10);
    CHECK(!r.source.start(at(9)));r.source.requestStop();CHECK(r.source.snapshot().state==S::Stopped&&!r.source.begin(WorkLane::User,at(9)));
    CHECK(r.source.snapshot().userTmacs.text()=="0.000006291456");
}
void validation(){
    for(unsigned mode=0;mode<11;++mode){Rig r;CHECK(r.source.begin(WorkLane::User,at()));auto b=batch();
        if(mode==0)b.workUnits=0;
        if(mode==1)b.attempts=0;
        if(mode==2)b.macsPerUnit=0;
        if(mode==3)b.workUnits=5;
        if(mode==4)b.appliedHostWaitMs=1001;
        if(mode==5)b.hostVerificationFailed=true;
        if(mode==6)b.observed.utcMs=-1;
        if(mode==7)b.observed.monotonicMs=0;
        if(mode==8)b.observed.utcMs+=2001;
        if(mode==9)b.observed.utcMs=253402300800000LL;
        if(mode==10){b.workUnits=UINT64_MAX;b.attempts=1;b.macsPerUnit=UINT64_MAX;}
        CHECK(!r.source.completed(b)&&r.source.snapshot().state==S::Faulted&&r.source.snapshot().batches==0&&r.source.snapshot().userTmacs.zero());
        CHECK(!r.source.end(true,at(2))&&!r.source.begin(WorkLane::User,at(3))&&!r.source.completed(batch(4)));}
    {Rig r;CHECK(!r.source.completed(batch())&&r.source.snapshot().status==R::Invalid);}
    {Rig r;CHECK(!r.source.end(true,at())&&r.source.snapshot().status==R::Attribution);}
    {Rig r;CHECK(r.source.begin(WorkLane::User,at())&&!r.source.begin(WorkLane::Donation,at())&&r.source.snapshot().status==R::Attribution);}
    {Rig r;CHECK(!r.source.begin(static_cast<WorkLane>(100),at())&&r.source.snapshot().status==R::Attribution);}
    {Rig r;CHECK(r.source.begin(WorkLane::User,at())&&r.source.completed(batch())&&!r.source.end(false,at(2)));
        CHECK(r.source.snapshot().batches==1&&r.source.snapshot().status==R::Discontinuity&&!r.source.begin(WorkLane::User,at(3)));}
    for(unsigned mode=0;mode<2;++mode){Rig r;CHECK(r.source.begin(WorkLane::User,at())&&r.source.completed(batch())&&r.source.end(true,at(2)));
        CHECK(r.source.begin(WorkLane::Donation,at(3)));auto b=batch(4);if(mode)b.macsPerUnit=524289;else b.workUnits=8;
        CHECK(!r.source.completed(b)&&r.source.snapshot().status==R::Discontinuity&&r.source.snapshot().batches==1&&r.source.snapshot().donationTmacs.zero());}
    {Rig r;CHECK(r.source.begin(WorkLane::User,at()));r.intent.stop();r.intent.explicitStart();
        CHECK(!r.source.completed(batch())&&r.source.snapshot().state==S::Stopped&&!r.source.start(at(2)));}
    {Rig r;auto end=at(1);end.utcMs+=1500;CHECK(r.source.begin(WorkLane::User,at())&&r.source.end(true,end));
        auto next=at(2);next.utcMs+=1500;CHECK(r.source.begin(WorkLane::Donation,next));auto b=batch(3);b.observed.utcMs+=2001;
        CHECK(!r.source.completed(b)&&r.source.snapshot().status==R::Clock);} // fresh run cannot reset original parent clock
}
void capacities(){
    for(unsigned mode=0;mode<6;++mode){SessionIntent intent;const auto token=intent.explicitStart();CompletedWorkPolicy p{true,1};uint64_t epoch=1;auto t=at();
        if(mode==0)p.enabled=false;
        if(mode==1)p.maximumRuns=0;
        if(mode==2)p.maximumRuns=8193;
        if(mode==3)epoch=0;
        if(mode==4)t.utcMs=-1;
        if(mode==5)t.monotonicMs=0;
        CompletedWorkCapture source(p,intent,token,epoch);CHECK(!source.start(t)&&!source.start(at())&&!source.begin(WorkLane::User,at()));}
    {Rig r;for(unsigned n=0;n<8192;++n)CHECK(r.source.begin(n%2?WorkLane::Donation:WorkLane::User,at(n))&&r.source.end(true,at(n)));
        CHECK(r.source.snapshot().runs==8192&&r.source.snapshot().batches==0&&!r.source.begin(WorkLane::User,at(8192))&&r.source.snapshot().status==R::Capacity);}
    {Rig r;CHECK(r.source.begin(WorkLane::User,at()));auto b=batch();b.workUnits=b.attempts=UINT64_MAX;b.macsPerUnit=1;
        CHECK(r.source.completed(b)&&r.source.snapshot().userTmacs.text()=="18446744.073709551615");b.observed=at(2);b.workUnits=b.attempts=1;
        CHECK(!r.source.completed(b)&&r.source.snapshot().status==R::Capacity&&r.source.snapshot().batches==1);}
    {Rig r;CHECK(r.source.begin(WorkLane::User,at()));auto b=batch();b.workUnits=9007199254740993ULL;b.attempts=1;b.macsPerUnit=1;
        CHECK(r.source.completed(b)&&r.source.snapshot().userTmacs.text()=="9007.199254740993");} // beyond exact binary64 integer range
}
}
int main(){originals();validation();capacities();return finishTests("exact completed-work originals");}
