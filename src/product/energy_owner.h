// Bounded in-memory session observations. No sensor calls, storage, source identity, mining actions or financial income.
#pragma once
#include "electricity_preferences.h"
#include "energy_view.h"
#include "electricity.h"
#include <functional>
#include <memory>
#include <optional>
namespace om {
enum class SessionEnergyState { Stopped, Running, ClockFault, Capacity };
enum class EnergyAdmission { Queued, Stopped, Invalid, Full, StaleGeneration };
struct SessionPowerInput { unsigned channel=0; uint64_t utcMs=0, monotonicMs=0; std::optional<uint32_t> milliwatts; };
struct SessionEnergySnapshot {
    SessionEnergyState state=SessionEnergyState::Stopped;
    uint64_t generation=0, revision=0, beginUtcMs=0, throughUtcMs=0, throughMonoMs=0;
    uint64_t coveredDeviceMs=0, requestedDeviceMs=0, pricedDeviceMs=0, dropped=0; // queue drops/discards, not a full tail/in-flight loss audit
    unsigned channels=0;
    PowerEvidence evidence;
    std::string knownEnergyNumerator="0", knownCostNumerator="0";
    EnergyView view;
};
class SessionEnergySink {
   public:
    virtual ~SessionEnergySink()=default;
    virtual bool start(uint64_t generation,uint64_t utcMs,uint64_t monotonicMs,unsigned channels,const ElectricityPreferences &preferences,PowerOrigin origin)=0;
    virtual EnergyAdmission submit(uint64_t generation,SessionPowerInput input)=0;
    virtual void requestStop()=0; // in-memory revocation only
    virtual void stop()=0; // join outside mining/render/safety loop
    virtual std::shared_ptr<const SessionEnergySnapshot> snapshot()const=0;
};
using SessionEnergyFactory=std::function<std::unique_ptr<SessionEnergySink>()>;
std::unique_ptr<SessionEnergySink> makeSessionEnergyOwner();
} // namespace om
