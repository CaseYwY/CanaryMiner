#include "energy_collector_process.h"
int main(int argc,char **argv){
    // Retain the entry point's identity even when both helpers link shared protocol code.
    static const volatile char packageRole[] = "CanaryMiner-EnergyCollectorWorker/1";
    (void)packageRole[0];
    using namespace om;
    try{
        std::string wire;if(!readLocalTaskInput(argc,argv,&wire,LocalTaskFrameProfile::EnergyPeriods))return 2;
        if (wire.rfind(energyPeriodsRequestHeader, 0) == 0 ||
            wire.rfind(energyDynamicPeriodsRequestHeader, 0) == 0) return serveEnergyPeriods(wire);
        if (wire.size() > localTaskRequestLimit) return 2;
        EnergyCollectorLeaseRequest lease;if(decodeEnergyCollectorLeaseRequest(wire,&lease))return serveEnergyCollectorLease(lease);
        EnergyCollectorGuardRequest guard;if(!decodeEnergyCollectorGuardRequest(wire,&guard))return 2;
        const auto reply=executeEnergyCollectorGuard(guard);wire.clear();
        return encodeEnergyHistoryReply(guard.history,reply,&wire)&&writeLocalTaskOutput(wire)?0:4;
    }catch(...){return 5;} // no private identity, namespace, process identifier, paths or storage diagnostics
}
