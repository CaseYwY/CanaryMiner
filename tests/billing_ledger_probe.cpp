#include "billing_fixture.h"
#include <iostream>
using namespace billing_fixture;
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const std::string mode=argv[1];auto b=binding();const auto path=std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    if(mode!="create"&&mode!="append"&&mode!="crash"&&mode!="inspect"&&mode!="mismatch")return 2;
    if(mode=="mismatch")b.plan.currency="EUR";
    BillingLedger ledger;const auto opened=ledger.open(path,b,T+2*hour,mode=="create"?BillingOpen::CreateNew:mode=="inspect"||mode=="mismatch"?BillingOpen::Inspect:BillingOpen::Existing);
    if(opened!=BillingStatus::Ok){std::cout<<"{\"opened\":"<<static_cast<int>(opened)<<"}\n";return 0;}
    std::optional<BillingStatus> applied;
    if(mode=="crash")BillingLedgerTestPeer::crashBeforeCommit(ledger);
    if(mode=="create"||mode=="append"||mode=="crash")applied=ledger.apply(chunk(mode=="create"?1:2),T+2*hour);
    const auto state=ledger.snapshot(T+2*hour);
    std::cout<<"{\"opened\":0,\"applied\":"<<(applied?std::to_string(static_cast<int>(*applied)):"null")<<",\"sequence\":"<<state.sequence<<",\"settled\":"<<(state.settledTotal?"true":"false")<<"}\n";
    return 0;
}
