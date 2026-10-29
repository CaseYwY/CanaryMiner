#include "energy_collector_process.h"
int main(int argc,char **argv){
    using namespace om;
    try{
        std::string wire;if(!readLocalTaskInput(argc,argv,&wire))return 2;
        EnergyCollectorLeaseRequest lease;if(decodeEnergyCollectorLeaseRequest(wire,&lease))return serveEnergyCollectorLease(lease);
        EnergyCollectorGuardRequest guard;if(!decodeEnergyCollectorGuardRequest(wire,&guard))return 2;
        const auto reply=executeEnergyCollectorGuard(guard);wire.clear();
        return encodeEnergyHistoryReply(guard.history,reply,&wire)&&writeLocalTaskOutput(wire)?0:4;
    }catch(...){return 5;} // no private identity, namespace, process identifier, paths or storage diagnostics
}
