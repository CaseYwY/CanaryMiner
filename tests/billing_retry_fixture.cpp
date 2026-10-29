#include "product/billing_retry_process.h"
#include <chrono>
#include <fstream>
#include <thread>
int main(int argc,char **argv){
    std::string wire;om::BillingRetryRequest r;uint64_t parent=0;
    if(!om::readLocalTaskInput(argc,argv,&wire)||!om::decodeBillingRetryRequest(wire,&r,&parent)||!om::bindLocalTaskParent(parent))return 2;
    const auto name=r.file.filename().u8string();
    if(name=="fixture.billing-retry-hang.db"){auto marker=r.file;marker+=".fixture-ready";{std::ofstream f(marker);f<<om::localTaskProcessId();}for(;;)std::this_thread::sleep_for(std::chrono::seconds(1));}
    auto reply=om::executeBillingRetryRequest(r);
    if(name=="fixture.billing-retry-lost.db")return om::writeLocalTaskOutput("[]")?0:3;
    if(name=="fixture.billing-retry-proof.db")reply.confirmedRevision=std::string(64,'f');
    if(!om::encodeBillingRetryReply(r,reply,&wire))return 4;
    return om::writeLocalTaskOutput(wire)?0:5;
}
