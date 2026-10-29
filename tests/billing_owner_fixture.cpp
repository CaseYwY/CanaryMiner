#include "product/billing_input_query.h"
#include "product/billing_retry_process.h"
#include <chrono>
#include <fstream>
#include <thread>
namespace {
void wait(const std::filesystem::path &file){
    auto ready=file;ready+=".fixture-ready";auto release=file;release+=".fixture-release";
    {std::ofstream f(ready);f<<om::localTaskProcessId();}
    while(!std::filesystem::exists(release))std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
}
int main(int argc,char **argv){
    std::string input,output;uint64_t parent=0;
    if(!om::readLocalTaskInput(argc,argv,&input))return 2;
    om::BillingQueryPageRequest query;om::BillingInputRequest request;om::BillingRetryRequest retry;
    if(om::decodeBillingRetryRequest(input,&retry,&parent)){
        if(!om::bindLocalTaskParent(parent))return 7;
        const auto reply=om::executeBillingRetryRequest(retry);
        if(reply.status!=om::BillingStatus::Ok&&reply.status!=om::BillingStatus::Replay)return 8;
        return om::writeLocalTaskOutput("[]")?0:9; // completed monetary retry, deliberately lost confirmation
    }
    if(om::decodeBillingQueryRequest(input,&query,&parent)){
        if(!om::bindLocalTaskParent(parent))return 3;
        const auto name=query.file.filename().u8string();
        if(name=="fixture.billing-owner-between.db"||name=="fixture.billing-owner-read-hang.db")wait(query.file);
        const auto reply=om::executeBillingQueryRequest(query);
        return om::encodeBillingQueryReply(query,reply,&output)&&om::writeLocalTaskOutput(output)?0:4;
    }
    if(!om::decodeBillingInputRequest(input,&request,&parent)||!om::bindLocalTaskParent(parent))return 5;
    const auto name=request.manifest.file.filename().u8string();
    if(name=="fixture.billing-owner-race.db"&&request.action==om::BillingInputAction::Begin)wait(request.manifest.file);
    const auto reply=om::executeBillingInputRequest(request);
    if((name=="fixture.billing-owner-late.db"||name=="fixture.billing-owner-clock.db")&&request.action==om::BillingInputAction::Commit)wait(request.manifest.file);
    return om::encodeBillingInputReply(request,reply,&output)&&om::writeLocalTaskOutput(output)?0:6;
}
