#include "billing_input_fixture.h"
#include "product/billing_compute_owner.h"
#include <chrono>
#include <thread>
using namespace billing_input_fixture;
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const auto root=std::filesystem::absolute(argv[2]);auto c=std::make_shared<BillingOwnerBinding>();c->file=root/"fixture.compute-bill.db";c->binding=binding(root/"fixture.compute-source.db");
    BillingRecoverySnapshot observed;{BillingRecoveryStore store;if(store.open(c->file,c->binding,now,BillingRecoveryOpen::CreateNew)!=S::Ok)return 3;observed=store.inspect(now);}
    const auto source=executeEnergyHistoryRequest(input(c->binding,1));if(source.applied!=EnergyHistoryStatus::Ok)return 4;
    auto s=std::make_shared<BillingOwnerSnapshot>();s->generation=s->revision=1;s->state=BillingOwnerState::Ready;s->reviewable=true;s->context=c;
    auto v=std::make_shared<BillingOwnerView>();v->stable=true;auto stage=std::make_shared<BillingInputSnapshot>();stage->status=S::Ok;v->stage=stage;v->monetary=std::make_shared<const BillingRecoverySnapshot>(observed);s->view=v;
    BillingComputeOwner owner(std::filesystem::absolute(argv[1]),c,{true,60000,512},[]{return BillingOwnerTime{now,10000};});
    if(!owner.start(1)||!owner.observe(1,s)||owner.notify(1,owner.snapshot()->revision,{anchor(source.checkpoint),T+1000,T+1000})!=BillingComputeAdmission::Queued)return 5;
    while(owner.snapshot()->pending)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return owner.snapshot()->reviewable?0:6;
}
