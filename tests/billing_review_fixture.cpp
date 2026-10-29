// Fixed synthetic native settings harness. No runtime file picker, production manifest, GPU or network.
#include "billing_input_fixture.h"
#include "product/billing_review_access.h"
#include "product/settings.h"
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>
using namespace billing_input_fixture;
static volatile sig_atomic_t stopped=0;
static void stop(int){stopped=1;}
int main(int argc,char **argv){
    if(argc<3)return 2;
    const std::string mode=argv[1];const bool candidate=mode=="--prepare-candidate"||mode=="--facts-candidate"||mode=="--settings-candidate";
    const auto root=std::filesystem::absolute(argv[argc-1]);const auto file=root/"fixture-billing-review.db";auto p=plan(file);
    if(candidate){p.context.binding=binding(root/"fixture-source.db");p.context.nowUtcMs=now;}
    if(mode=="--facts"||mode=="--facts-candidate"){const auto s=inspect(p);std::cout<<static_cast<unsigned>(s.state)<<" "<<s.committed.sequence<<"\n";return 0;}
    if(mode=="--prepare-candidate"){
        if(std::filesystem::exists(file))return 3;
        std::filesystem::create_directories(root);BillingRecoveryStore store;
        if(store.open(file,p.context.binding,now,BillingRecoveryOpen::CreateNew)!=S::Ok)return 4;
        return executeEnergyHistoryRequest(input(p.context.binding,1)).applied==EnergyHistoryStatus::Ok?0:5;
    }
    if(mode=="--prepare"){
        if(std::filesystem::exists(file))return 3;
        std::filesystem::create_directories(root);std::string payload;auto r=request(p,&payload);auto reply=executeBillingInputRequest(r);
        if(reply.status!=S::Ok)return 4;
        for(size_t offset=0;offset<payload.size();offset+=billingInputPageBytes){r.action=A::Append;r.offset=offset;r.data=payload.substr(offset,billingInputPageBytes);r.expectedRevision=reply.revision;reply=executeBillingInputRequest(r);if(reply.status!=S::Ok)return 5;}
        return 0;
    }
    if((mode!="--settings"||argc!=7)&&(mode!="--settings-candidate"||argc!=8))return 6;
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    BillingOwnerHelpers helpers{std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4]),std::filesystem::absolute(argv[5])};
    BillingOwner owner(helpers,{file,p.context.binding,p.context.limits},{true,10000},[candidate]{return BillingOwnerTime{candidate?now:at,10000};});if(!owner.start(1))return 7;
    if(candidate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(owner.snapshot()->pending&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if(!owner.snapshot()->reviewable)return 8;
        BillingComputeOwner compute(std::filesystem::absolute(argv[6]),owner.snapshot()->context,{true,10000,512},[]{return BillingOwnerTime{now,10000};});if(!compute.start(1)||!compute.observe(1,owner.snapshot()))return 9;
        const auto q=query(p.context.binding,{0,sha256Hex(energy_history_detail::bindingText(p.context.binding.source->binding))});const auto source=executeEnergyTraceRequest(q).source.checkpoint;
        if(compute.notify(1,compute.snapshot()->revision,{anchor(source),T+1000,T+1000})!=BillingComputeAdmission::Queued)return 10;
        const auto result=runSettingsCommand(root.u8string(),&stopped,false,false,{},{},{},{},{},billingCandidateReview(owner,compute));owner.stop();compute.stop();return result;
    }
    const auto result=runSettingsCommand(root.u8string(),&stopped,false,false,{},{},{},{},{},billingOwnerReview(owner));owner.stop();return result;
}
