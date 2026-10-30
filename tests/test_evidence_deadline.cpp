// Original-revision expiry arithmetic, including a nondecreasing clock pair whose skew converges.
#include "product/evidence_deadline.h"
#include "product_test_util.h"
using namespace om;
int main(){
    const GameGuardTime observed{10000,1000};
    {EvidenceDeadline bound;CHECK(bound.clip(1,{10010,1010},observed,1000)==2000);
        CHECK(bound.clip(1,{10210,1020},observed,1000)==1810);
        CHECK(bound.clip(1,{10220,1120},observed,1000)==1810);
        CHECK(bound.clip(1,{10911,1811},observed,1000)==0);
        CHECK(bound.clip(1,{10920,1820},observed,1000)==0);
        CHECK(bound.clip(2,{10930,1830},{10930,1830},1000)==2830);
        CHECK(bound.clip(1,{10940,1840},observed,1000)==0);
        CHECK(bound.clip(2,{10950,1850},{10930,1830},1000)==2830);}
    {EvidenceDeadline bound;CHECK(bound.clip(7,{10000,1000},observed,1000)==2000);
        CHECK(bound.clip(7,{10001,1001},{10000,1001},1000)==0);
        CHECK(bound.clip(7,{10002,1002},observed,1000)==0);}
    {EvidenceDeadline bound;CHECK(bound.clip(1,{10000,1000},observed,1000)==2000);
        CHECK(bound.clip(1,{10010,1010},observed,1001)==0);CHECK(bound.clip(1,{10020,1020},observed,1000)==0);}
    {EvidenceDeadline bound;CHECK(bound.clip(0,observed,observed,1000)==0);CHECK(bound.clip(1,{9999,999},observed,1000)==0);
        CHECK(bound.clip(1,observed,observed,1000)==0);CHECK(bound.clip(2,{10001,1001},{10001,1001},1000)==2001);}
    {EvidenceDeadline bound;CHECK(bound.clip(1,{11000,2000},observed,1000)==0);CHECK(bound.clip(2,observed,observed,0)==0);}
    {EvidenceDeadline bound;CHECK(bound.clip(1,{10000,UINT64_MAX-10},{10000,UINT64_MAX-10},1000)==0);}
    {EvidenceDeadline safety,detection,profit;CHECK(safety.clip(1,observed,observed,1000)==2000);
        CHECK(detection.clip(1,observed,observed,500)==1500);CHECK(profit.clip(1,observed,observed,2000)==3000);
        CHECK(detection.clip(1,{10500,1500},observed,500)==0);CHECK(safety.clip(1,{10500,1500},observed,1000)==2000);}
    {EvidenceDeadline bound;CHECK(bound.clip(1,{10200,1010},observed,1000)==1810);
        CHECK(bound.clip(2,{10210,1110},observed,1000)==1810);
        CHECK(bound.clip(3,{10920,1820},observed,1000)==0);CHECK(bound.clip(4,{10930,1830},observed,1000)==0);}
    // Over all forward-moving clock pairs, the same original's returned nonzero ceiling never increases.
    for(unsigned faster=0;faster<16;++faster){EvidenceDeadline bound;uint64_t last=UINT64_MAX;bool expired=false;
        for(unsigned step=0;step<40;++step){const GameGuardTime now{10000+static_cast<int64_t>(step*(faster+1)),1000+step*10u};
            const auto deadline=bound.clip(1,now,observed,300);
            CHECK(!deadline||(!expired&&deadline<=last));if(deadline)last=deadline;else expired=true;}}
    return finishTests("immutable evidence deadlines");
}
