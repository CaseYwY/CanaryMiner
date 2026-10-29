#include "product/billing_query_process.h"
#include "product/hash.h"
#include <chrono>
#include <fstream>
#include <thread>
int main(int argc,char **argv){
    std::string wire;om::BillingQueryPageRequest request;uint64_t parent=0;
    if(!om::readLocalTaskInput(argc,argv,&wire)||!om::decodeBillingQueryRequest(wire,&request,&parent)||!om::bindLocalTaskParent(parent))return 2;
    const auto name=request.file.filename().u8string();
    if(name=="fixture.billing-query-hang.db"){
        auto marker=request.file;marker+=".fixture-ready";{std::ofstream f(marker);f<<om::localTaskProcessId();}
        for(;;)std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if(name=="fixture.billing-query-exit.db")return 3;
    if(name=="fixture.billing-query-bad.db")return om::writeLocalTaskOutput("[]")?0:4;
    auto reply=om::executeBillingQueryRequest(request);
    if(name=="fixture.billing-query-change.db"&&request.offset){reply={};reply.status=om::BillingStatus::Conflict;}
    if(name=="fixture.billing-query-damaged.db"&&reply.status==om::BillingStatus::Ok){reply.data.back()='x';reply.viewDigest=om::sha256Hex(reply.data);}
    if(!om::encodeBillingQueryReply(request,reply,&wire))return 5;
    if(name=="fixture.billing-query-extra.db")wire+='x';
    return om::writeLocalTaskOutput(wire)?0:6;
}
