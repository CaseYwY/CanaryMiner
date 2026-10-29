#include "billing_input_fixture.h"
#include "product/billing_compute_owner.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
using namespace billing_input_fixture;
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const auto root=std::filesystem::path(makeScratchDir("billing-compute-latch"));
    for(bool invalidate:{false,true}){auto c=std::make_shared<BillingOwnerBinding>();c->file=root/(invalidate?"revoked.db":"current.db");c->binding=binding(root/(invalidate?"revoked-source.db":"current-source.db"));
        BillingRecoverySnapshot observed;{BillingRecoveryStore store;CHECK(store.open(c->file,c->binding,now,BillingRecoveryOpen::CreateNew)==S::Ok);observed=store.inspect(now);}
        const auto source=executeEnergyHistoryRequest(input(c->binding,1));CHECK(source.applied==EnergyHistoryStatus::Ok);
        auto review=std::make_shared<BillingOwnerSnapshot>();review->generation=1;review->revision=1;review->state=BillingOwnerState::Ready;review->reviewable=true;review->context=c;
        auto v=std::make_shared<BillingOwnerView>();v->stable=true;auto stage=std::make_shared<BillingInputSnapshot>();stage->status=S::Ok;v->stage=stage;v->monetary=std::make_shared<const BillingRecoverySnapshot>(observed);review->view=v;
        BillingComputeOwner owner(std::filesystem::absolute(argv[invalidate?2:1]),c,{true,10000,512},[]{return BillingOwnerTime{now,10000};});CHECK(owner.start(1)&&owner.observe(1,review));
        if(!invalidate)BillingComputeTestPeer::unreapedNext(owner);
        CHECK(owner.notify(1,owner.snapshot()->revision,{anchor(source.checkpoint),T+1000,T+1000})==BillingComputeAdmission::Queued);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        if(invalidate){while(!std::filesystem::exists(side(c->binding.source->file,".fixture-ready"))&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            CHECK(std::filesystem::exists(side(c->binding.source->file,".fixture-ready")));BillingComputeTestPeer::unreapedNext(owner);CHECK(owner.invalidate(1));}
        while(owner.snapshot()->pending&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto s=owner.snapshot();CHECK(s->state==BillingComputeState::Faulted&&s->transport==LocalTaskStatus::Unreaped&&!s->reviewable&&!s->candidate);
        CHECK(owner.refresh(1,s->revision)==BillingComputeAdmission::Stopped&&!owner.start(2));owner.stop();
    }
    std::filesystem::remove_all(root);return finishTests("billing compute unreaped latch");
}
