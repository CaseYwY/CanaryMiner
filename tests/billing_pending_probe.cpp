#include "billing_source_fixture.h"
#include "product/billing_recovery.h"
#include "product/billing_model.h"
#include <iostream>
using namespace billing_source_fixture;
int main(int argc,char **argv){
    if(argc!=3)return 2;
    using S=BillingStatus;using O=BillingRecoveryOpen;
    const std::string action=argv[1];const auto file=std::filesystem::absolute(std::filesystem::u8path(argv[2]));auto source=file;source+=".source.db";const auto b=binding(source);
    BillingRecoveryStore store;const auto opened=store.open(file,b,now+(action=="retry"?1000:0),action=="init"?O::CreateNew:action=="inspect"?O::QueryOnly:O::Existing);
    std::optional<S> applied;
    if(opened==S::Ok&&action!="init"&&action!="inspect"){
        if(action=="retry"||action=="retry-changed"||action=="retry-stale"){
            const auto s=store.inspect(now+1000);if(!s.original)return 3;auto c=*s.original;
            if(action=="retry-changed")c.receipt->request.source.nowUtcMs++;
            applied=store.retry(c,action=="retry-stale"?std::string(64,'f'):s.revision,now+1000);
        }else{
            if(executeEnergyHistoryRequest(input(b,1)).applied!=EnergyHistoryStatus::Ok)return 4;
            const auto q=query(b,{0,sha256Hex(energy_history_detail::bindingText(b.source->binding))});const auto reply=executeEnergyTraceRequest(q);BillingChunk c;
            if(makeBillingSourceChunk(1,q,reply,&c)!=S::Ok)return 5;
            if(action=="rejected"||action=="failed-retry")c.receipt->request.anchor->digest=std::string(64,'f');
            // A syntactically valid but wrong monetary source anchor is used for preserved failed retry.
            if(action=="failed-retry"||action=="rejected"){c.receipt->request.anchor->sequence=1;c.receipt->request.anchor->digest=reply.source.checkpoint.digest;}
#ifdef CANARY_BILLING_TESTING
            auto point=BillingRecoveryTestPoint::None;
            if(action=="temp")point=BillingRecoveryTestPoint::BeforeJournalReplace;
            if(action=="prepared"||action=="failed-retry")point=BillingRecoveryTestPoint::AfterPrepared;
            if(action=="precommit")point=BillingRecoveryTestPoint::BeforeCommit;
            if(action=="committed")point=BillingRecoveryTestPoint::AfterCommit;
            if(action=="settled")point=BillingRecoveryTestPoint::AfterSettled;
            if(action=="settle-failure")point=BillingRecoveryTestPoint::FailSettlement;
            BillingRecoveryTestPeer::interrupt(store,point);
#endif
            applied=store.submit(c,now);
        }
    }
    const auto s=store.inspect(now+1000);
    std::cout<<"{\"opened\":"<<static_cast<int>(opened)<<",\"applied\":"<<(applied?std::to_string(static_cast<int>(*applied)):"null")<<",\"status\":"<<static_cast<int>(s.status)<<",\"state\":"<<static_cast<int>(s.state)
        <<",\"sequence\":"<<s.committed.sequence<<",\"revision\":\""<<s.revision<<"\",\"digest\":\""<<s.committed.digest<<"\",\"original\":"<<(s.original?"true":"false")<<"}\n";
}
