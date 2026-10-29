#include "product/energy_trace_process.h"
#include <chrono>
#include <fstream>
#include <thread>
int main(int argc,char **argv){
    std::string wire;if(!om::readLocalTaskInput(argc,argv,&wire))return 2;
    om::EnergyTraceRequest request;uint64_t pid=0;if(!om::decodeEnergyTraceRequest(wire,&request,&pid)||!om::bindLocalTaskParent(pid))return 3;
    const auto name=request.source.file.filename().u8string();
    if(name=="fixture.energy-trace-hang.db"){
        auto marker=request.source.file;marker+=".fixture-ready";{std::ofstream ready(marker);ready<<om::localTaskProcessId();}
        for(;;)std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if(name=="fixture.energy-trace-exit.db")return 7;
    if(name=="fixture.energy-trace-bad.db")return om::writeLocalTaskOutput("CMETA1\n0\nnull")?0:8;
    const auto reply=om::executeEnergyTraceRequest(request);if(!om::encodeEnergyTraceReply(request,reply,&wire))return 4;
    if(name=="fixture.energy-trace-extra.db")wire+="unexpected";
    return om::writeLocalTaskOutput(wire)?0:5;
}
