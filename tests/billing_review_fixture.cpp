// Fixed synthetic native settings harness. No runtime file picker, production manifest, GPU or network.
#include "billing_input_fixture.h"
#include "product/billing_review_access.h"
#include "product/settings.h"
#include <csignal>
#include <iostream>
using namespace billing_input_fixture;
static volatile sig_atomic_t stopped=0;
static void stop(int){stopped=1;}
int main(int argc,char **argv){
    if(argc<3)return 2;
    const std::string mode=argv[1];const auto root=std::filesystem::absolute(argv[argc-1]);const auto file=root/"fixture-billing-review.db";auto p=plan(file);
    if(mode=="--facts"){const auto s=inspect(p);std::cout<<static_cast<unsigned>(s.state)<<" "<<s.committed.sequence<<"\n";return 0;}
    if(mode=="--prepare"){
        if(std::filesystem::exists(file))return 3;
        std::filesystem::create_directories(root);std::string payload;auto r=request(p,&payload);auto reply=executeBillingInputRequest(r);
        if(reply.status!=S::Ok)return 4;
        for(size_t offset=0;offset<payload.size();offset+=billingInputPageBytes){r.action=A::Append;r.offset=offset;r.data=payload.substr(offset,billingInputPageBytes);r.expectedRevision=reply.revision;reply=executeBillingInputRequest(r);if(reply.status!=S::Ok)return 5;}
        return 0;
    }
    if(mode!="--settings"||argc!=7)return 6;
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    BillingOwnerHelpers helpers{std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4]),std::filesystem::absolute(argv[5])};
    BillingOwner owner(helpers,{file,p.context.binding,p.context.limits},{true,10000},[]{return BillingOwnerTime{at,10000};});if(!owner.start(1))return 7;
    const auto result=runSettingsCommand(root.u8string(),&stopped,false,false,{},{},{},{},{},billingOwnerReview(owner));owner.stop();return result;
}
