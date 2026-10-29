#include "product/billing_input_process.h"
#include <chrono>
#include <fstream>
#include <thread>
int main(int argc,char **argv){
    std::string wire;om::BillingInputRequest r;uint64_t parent=0;
    if(!om::readLocalTaskInput(argc,argv,&wire)||!om::decodeBillingInputRequest(wire,&r,&parent)||!om::bindLocalTaskParent(parent))return 2;
    const auto name=r.manifest.file.filename().u8string();
    if(name=="fixture.billing-input-hang.db"){
        auto marker=r.manifest.file;marker+=".fixture-ready";{std::ofstream f(marker);f<<om::localTaskProcessId();}
        for(;;)std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    auto reply=om::executeBillingInputRequest(r);
    if(name=="fixture.billing-input-proof.db"&&reply.certificate)reply.certificate->digest=std::string(64,'f');
    if(name=="fixture.billing-input-lost.db"&&r.action==om::BillingInputAction::Commit)return om::writeLocalTaskOutput("[]")?0:3;
    if(!om::encodeBillingInputReply(r,reply,&wire))return 4;
    return om::writeLocalTaskOutput(wire)?0:5;
}
