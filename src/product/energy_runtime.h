// One explicit parent owns authorization controls and, optionally, a lifetime-fenced collector.
#pragma once
#include "energy_authorization_owner.h"

namespace om {
class EnergyRuntime : public EnergyCaptureSink {
   public:
    virtual std::shared_ptr<const EnergyAuthorizationSnapshot> authorizationSnapshot()const=0;
    virtual EnergyHistoryAdmission authorize(uint64_t generation,uint64_t revision,const EnergyAuthorizationCommand &command)=0;
    virtual EnergyHistoryAdmission acknowledge(uint64_t generation,uint64_t revision)=0;
    virtual std::vector<std::string> selectedDevices()const=0;
};
// mining=false is the idle settings command. CLI suppression still permits next-parent settings edits.
using EnergyRuntimeFactory=std::function<std::unique_ptr<EnergyRuntime>(bool mining,bool cliNoEnergy)>;
std::unique_ptr<EnergyRuntime> makeEnergyRuntime(std::filesystem::path authorizationHelper,std::filesystem::path collectorHelper,
    std::filesystem::path authorizationFile,bool mining,bool cliNoEnergy,unsigned timeoutMs=10000,EnergyHistoryClock clock={});
// OS module path, never argv[0], PATH, environment or a configurable executable. No filesystem probes.
struct EnergyRuntimePaths {std::filesystem::path authorizationHelper,collectorHelper,authorizationFile;};
std::filesystem::path energyRuntimeExecutable();
std::optional<EnergyRuntimePaths> energyRuntimePaths(const std::filesystem::path &executable,const std::filesystem::path &absoluteStateDir);
std::unique_ptr<EnergyRuntime> makeNativeEnergyRuntime(const std::string &stateDir,bool mining,bool cliNoEnergy);
} // namespace om
