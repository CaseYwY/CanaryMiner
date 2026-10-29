#include "energy_trace_process.h"
int main(int argc,char **argv){
    try{
        std::string wire;if(!om::readLocalTaskInput(argc,argv,&wire))return 2;
        om::EnergyTraceRequest request;uint64_t pid=0;
        if(!om::decodeEnergyTraceRequest(wire,&request,&pid)||!om::bindLocalTaskParent(pid))return 3;
        const auto reply=om::executeEnergyTraceRequest(request);
        if(!om::encodeEnergyTraceReply(request,reply,&wire))return 4;
        return om::writeLocalTaskOutput(wire)?0:5;
    }catch(...){return 6;}
}
