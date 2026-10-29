#include "billing_input_fixture.h"
#include "product/billing_retry_process.h"
#include <iostream>
using namespace billing_input_fixture;
int main(int argc,char **argv){
    if(argc!=7)return 2;
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]),retry=std::filesystem::absolute(argv[3]);
    const auto file=std::filesystem::absolute(std::filesystem::u8path(argv[4]));const std::string action=argv[5],mode=argv[6];std::atomic<bool> cancel{false};
    auto p=plan(file);p.context.binding=binding(side(file,".source.db"));p.context.nowUtcMs=now;
    if(action=="init"){BillingRecoveryStore store;const auto opened=store.open(file,p.context.binding,now,BillingRecoveryOpen::CreateNew);std::cout<<"{\"status\":"<<static_cast<unsigned>(opened)<<"}\n";return 0;}
    if(action=="query"){const auto s=inspect(p);std::cout<<"{\"status\":"<<static_cast<unsigned>(s.status)<<",\"state\":"<<static_cast<unsigned>(s.state)<<",\"sequence\":"<<s.committed.sequence<<"}\n";return 0;}
    if(action=="retry"){p.context.nowUtcMs=now+1000;const auto s=inspect(p);BillingRetryProcessClient client(retry);const auto result=client.transact(p.context,s,cancel,10000);
        std::cout<<"{\"transport\":"<<static_cast<unsigned>(result.transport)<<",\"status\":"<<(result.applied?std::to_string(static_cast<unsigned>(*result.applied)):"null")<<",\"confirmed\":"<<(result.confirmed?"true":"false")<<"}\n";return 0;}
    BillingInputProcessClient client(mode=="fixture"?fixture:helper);BillingInputResult result;
    if(action=="create"||action=="parent-probe"){
        result=client.transact(p,"",cancel,action=="parent-probe"?60000:10000);
    }else if(action=="submit"||action=="stage"){
        p.prior=inspect(p);if(p.prior->status!=S::Ok)return 3;
        if(executeEnergyHistoryRequest(input(p.context.binding,1)).applied!=EnergyHistoryStatus::Ok)return 4;
        const auto q=query(p.context.binding,{0,sha256Hex(energy_history_detail::bindingText(p.context.binding.source->binding))});BillingChunk chunk;
        if(makeBillingSourceChunk(1,q,executeEnergyTraceRequest(q),&chunk)!=S::Ok)return 5;
        p.chunk=std::move(chunk);
        std::filesystem::remove(p.context.binding.source->file);std::filesystem::remove(journal(p.context.binding.source->file));
        if(action=="submit")result=client.transact(p,"",cancel,10000);
        else{std::string payload;auto r=request(p,&payload);result=client.exchange(r,cancel,10000);if(!result.reply||result.reply->status!=S::Ok)return 6;
            for(size_t offset=0;offset<payload.size();offset+=billingInputPageBytes){r.expectedRevision=result.reply->revision;r.action=A::Append;r.offset=offset;r.data=payload.substr(offset,billingInputPageBytes);result=client.exchange(r,cancel,10000);if(!result.reply||result.reply->status!=S::Ok)return 7;}}
    }else{
        BillingInputRequest r;r.generation=r.ticket=1;if(!readManifest(file,r.manifest))return 8;
        r.action=A::Inspect;result=client.exchange(r,cancel,10000);
        if(action!="inspect"&&result.reply&&result.reply->status==S::Ok){r.expectedRevision=result.reply->revision;r.action=action=="commit"?A::Commit:A::Reconcile;result=client.exchange(r,cancel,10000);}
    }
    std::cout<<"{\"transport\":"<<static_cast<unsigned>(result.transport)<<",\"status\":"<<(result.reply?std::to_string(static_cast<unsigned>(result.reply->status)):"null")
        <<",\"phase\":"<<(result.reply?std::to_string(static_cast<unsigned>(result.reply->phase)):"null")<<",\"confirmed\":"<<(result.confirmed?"true":"false")
        <<",\"received\":"<<(result.reply?result.reply->received:0)<<",\"issued\":"<<(result.commitIssued?"true":"false")<<"}\n";
}
