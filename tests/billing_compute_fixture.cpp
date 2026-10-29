// Synthetic readonly helper stall after a complete source read, before reply publication.
#include "product/energy_trace_process.h"
#include <chrono>
#include <fstream>
#include <thread>
int main(int argc,char **argv){
    std::string wire,output;uint64_t parent=0;om::EnergyTraceRequest request;
    if(!om::readLocalTaskInput(argc,argv,&wire)||!om::decodeEnergyTraceRequest(wire,&request,&parent)||!om::bindLocalTaskParent(parent))return 2;
    const auto reply=om::executeEnergyTraceRequest(request);
    auto ready=request.source.file;ready+=".fixture-ready";auto release=request.source.file;release+=".fixture-release";
    {std::ofstream f(ready);f<<om::localTaskProcessId();}
    while(!std::filesystem::exists(release))std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return om::encodeEnergyTraceReply(request,reply,&output)&&om::writeLocalTaskOutput(output)?0:3;
}
