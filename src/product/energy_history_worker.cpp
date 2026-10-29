#include "energy_history_process.h"
#ifdef CANARY_ENERGY_HISTORY_WORKER_TESTING
#include <chrono>
#include <thread>
#include <fstream>
#endif

int main(int argc,char **argv){
    using namespace om;
    try{
        std::string wire;EnergyHistoryRequest request;
        if(!readLocalTaskInput(argc,argv,&wire)||!decodeEnergyHistoryRequest(wire,&request))return 2;
#ifdef CANARY_ENERGY_HISTORY_WORKER_TESTING
        auto marker=request.file;marker+=".fixture-ready";
        if(request.file.filename()=="fixture-energy-stall-open.db"||(request.batch&&request.batch->id=="fixture.stall-before")){
            std::ofstream(marker).put('1');std::this_thread::sleep_for(std::chrono::seconds(60));}
#endif
        const auto reply=executeEnergyHistoryRequest(request);
#ifdef CANARY_ENERGY_HISTORY_WORKER_TESTING
        if(request.batch&&request.batch->id=="fixture.stall-after"&&reply.applied==EnergyHistoryStatus::Ok){
            std::ofstream(marker).put('1');std::this_thread::sleep_for(std::chrono::seconds(60));}
        if(request.batch&&request.batch->id=="fixture.bad-ack")return writeLocalTaskOutput("[\"CMEA1\"]")?0:4;
#endif
        wire.clear();return encodeEnergyHistoryReply(request,reply,&wire)&&writeLocalTaskOutput(wire)?0:4;
    }catch(...){return 5;} // never print readings, channel keys, paths or storage diagnostics
}
