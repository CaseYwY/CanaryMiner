// Pure selected-device core-temperature limiter. Passing it is not complete safety or mining authority.
#pragma once
#include "safety_telemetry_owner.h"
namespace om {
// NVML's original event-mask dialect: idle, application clocks, software power cap, hardware slowdown,
// sync boost, software thermal, hardware thermal, hardware power brake, and display clocks.
constexpr uint64_t coreThermalKnownEvents=0x1ff,coreThermalHoldEvents=0xe8;
struct CoreThermalLimit {
    std::string uuid;
    int pauseAtC=0,resumeAtOrBelowC=0; // mandatory explicit hysteresis pair; equal defaults are invalid
};
struct CoreThermalPolicy {
    bool enabled=false;
    unsigned maximumAgeMs=2000,maximumGapMs=2000,coolingMs=30000,minimumCoolSamples=2;
    std::vector<CoreThermalLimit> devices; // frozen ordered full UUIDs; no model-wide temperature default
};
enum class CoreThermalState { Dormant, Disabled, Unavailable, Unsupported, Cooling, CoreWithinLimits, InvalidPolicy, ClockFault, Faulted, Stopped };
struct CoreThermalSnapshot {
    CoreThermalState state=CoreThermalState::Dormant;
    bool sourceUnreaped=false;
    uint64_t sourceRevision=0,validUntilMonoMs=0;
    unsigned coolSamples=0;
    std::optional<GameGuardTime> observed,coolingSince;
};
bool validCoreThermalPolicy(const CoreThermalPolicy &);
// Single-owner, one explicit session. It performs no I/O and cannot start/restart its source or a worker.
class CoreThermalGuard {
   public:
    CoreThermalGuard(CoreThermalPolicy,const SessionIntent &,SessionToken);
    CoreThermalGuard(const CoreThermalGuard &)=delete;
    CoreThermalGuard &operator=(const CoreThermalGuard &)=delete;
    bool start(GameGuardTime);
    const CoreThermalSnapshot &update(GameGuardTime,const SafetyTelemetrySnapshot &);
    const CoreThermalSnapshot &snapshot()const{return view_;} // retain the original deadline; never assume freshness
    void requestStop();
   private:
    CoreThermalPolicy policy_;const SessionIntent &intent_;SessionToken token_;
    CoreThermalSnapshot view_;bool attempted_=false,terminal_=false,admitted_=false;
    GameGuardTime anchor_,last_;uint64_t sourceDeadline_=0;std::optional<GameGuardTime> previousFinished_;
    std::optional<SafetyTelemetryRequest> previousRequest_;
    std::optional<SafetyTelemetryReply> previousGood_;
    std::vector<uint64_t> capabilities_;std::string previousReceipt_;
    void hold(CoreThermalState);
    void halt(CoreThermalState);
    bool time(GameGuardTime);
};
} // namespace om
