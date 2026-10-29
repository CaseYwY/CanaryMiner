#include "energy_authorization.h"
#ifdef CANARY_ENERGY_AUTHORIZATION_WORKER_TESTING
#include <chrono>
#include <fstream>
#include <thread>
#endif
int main(int argc,char **argv){
    using namespace om;
    try{
        std::string wire;EnergyAuthorizationRequest request;
        if(!readLocalTaskInput(argc,argv,&wire)||!decodeEnergyAuthorizationRequest(wire,&request))return 2;
#ifdef CANARY_ENERGY_AUTHORIZATION_WORKER_TESTING
        auto marker=request.file;marker+=".fixture-ready";
        if(request.action!=EnergyAuthorizationAction::Load&&request.file.filename()=="fixture-stall-before.json"){
            std::ofstream(marker).put('1');std::this_thread::sleep_for(std::chrono::seconds(60));}
#endif
        const auto reply=executeEnergyAuthorizationRequest(request);
#ifdef CANARY_ENERGY_AUTHORIZATION_WORKER_TESTING
        if(request.action!=EnergyAuthorizationAction::Load&&request.file.filename()=="fixture-stall-after.json"){
            std::ofstream(marker).put('1');std::this_thread::sleep_for(std::chrono::seconds(60));}
        if(request.action!=EnergyAuthorizationAction::Load&&request.file.filename()=="fixture-bad-ack.json")return writeLocalTaskOutput("[\"CMEPA1\"]")?0:4;
#endif
        wire.clear();return encodeEnergyAuthorizationReply(request,reply,&wire)&&writeLocalTaskOutput(wire)?0:4;
    }catch(...){return 5;} // no private paths, UUIDs, namespaces or entropy diagnostics on stdout/stderr
}
