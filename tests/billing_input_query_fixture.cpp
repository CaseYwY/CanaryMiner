#include "product/billing_input_query.h"
#include "product/hash.h"
#include <chrono>
#include <fstream>
#include <thread>
int main(int argc,char **argv){
    std::string wire;om::BillingQueryPageRequest r;uint64_t parent=0;
    if(!om::readLocalTaskInput(argc,argv,&wire)||!om::decodeBillingInputQueryRequest(wire,&r,&parent)||!om::bindLocalTaskParent(parent))return 2;
    const auto name=r.file.filename().u8string();
    if(name=="fixture.billing-stage-query-hang.db"){
        auto marker=r.file;marker+=".fixture-ready";{std::ofstream f(marker);f<<om::localTaskProcessId();}
        for(;;)std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    auto reply=om::executeBillingInputQuery(r);
    if(name=="fixture.billing-stage-query-change.db"&&r.offset){reply={};reply.status=om::BillingStatus::Conflict;}
    if(name=="fixture.billing-stage-query-proof.db"&&reply.status==om::BillingStatus::Ok){reply.data.back()='x';reply.viewDigest=om::sha256Hex(reply.data);}
    if(!om::encodeBillingInputQueryReply(r,reply,&wire))return 3;
    return om::writeLocalTaskOutput(wire)?0:4;
}
