#include "billing_source_fixture.h"
#include <iostream>
using namespace billing_source_fixture;
int main(int argc,char **argv){
    if(argc!=4)return 2;
    const std::string action=argv[1];if(action!="create"&&action!="append"&&action!="crash"&&action!="replay"&&action!="inspect")return 2;
    const auto file=std::filesystem::absolute(std::filesystem::u8path(argv[2])),source=std::filesystem::absolute(std::filesystem::u8path(argv[3]));const auto b=binding(source);
    BillingLedger ledger;const auto opened=ledger.open(file,b,now,action=="create"?BillingOpen::CreateNew:action=="inspect"?BillingOpen::Inspect:BillingOpen::Existing);
    std::optional<BillingStatus> applied;
    if(opened==BillingStatus::Ok&&action!="inspect"){
        const unsigned seq=action=="create"?1:2;
        if(seq==1){if(executeEnergyHistoryRequest(input(b,seq)).applied!=EnergyHistoryStatus::Ok)return 3;}
        else{auto inspect=input(b,seq);inspect.batch.reset();inspect.recovery=EnergyRecoveryMode::Inspect;const auto prior=executeEnergyHistoryRequest(inspect);
            if(prior.opened!=EnergyHistoryStatus::Ok)return 3;
            if(prior.checkpoint.sequence==1&&executeEnergyHistoryRequest(input(b,seq)).applied!=EnergyHistoryStatus::Ok)return 3;}
        auto q=ledger.sourceQuery(T+seq*1000,now);
        if(action=="replay"){const auto last=executeEnergyTraceRequest(query(b,{0,sha256Hex(energy_history_detail::bindingText(b.source->binding))}));
            if(!last.source.recovery||last.source.checkpoint.sequence!=2)return 4;
            q=query(b,{last.source.recovery->priorSequence,last.source.recovery->priorDigest},T+1000,T+2000);}
        if(!q)return 5;
        const auto reply=executeEnergyTraceRequest(*q);BillingChunk chunk;if(makeBillingSourceChunk(seq,*q,reply,&chunk)!=BillingStatus::Ok)return 6;
#ifdef CANARY_BILLING_TESTING
        if(action=="crash")BillingLedgerTestPeer::crashBeforeCommit(ledger);
#endif
        applied=ledger.apply(chunk,now);
    }
    const auto s=ledger.snapshot(now);
    std::cout<<"{\"opened\":"<<static_cast<int>(opened)<<",\"applied\":"<<(applied?std::to_string(static_cast<int>(*applied)):"null")<<",\"sequence\":"<<s.sequence<<",\"sourceSequence\":"<<(s.source?std::to_string(s.source->checkpoint.sequence):"null")<<",\"digest\":\""<<s.digest<<"\"}\n";
}
