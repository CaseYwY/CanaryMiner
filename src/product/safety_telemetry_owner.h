// Fixed one-flight background owner. Immutable observations carry no GameSafety or mining authority.
#pragma once
#include "safety_telemetry.h"
namespace om {
enum class SafetyTelemetryOwnerState { Dormant, Disabled, Waiting, Ready, Stopped, ClockFault, Faulted };
struct SafetyTelemetryPolicy {bool enabled=false;unsigned pollMs=500,helperTimeoutMs=1000,collectionBudgetMs=500;};
struct SafetyTelemetrySnapshot {
    SafetyTelemetryOwnerState state=SafetyTelemetryOwnerState::Dormant;
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    SafetyTelemetryStatus status=SafetyTelemetryStatus::Unavailable;
    uint64_t completed=0;
    bool pending=false,unreaped=false;
    std::optional<SafetyTelemetryRequest> request;
    std::optional<SafetyTelemetryReply> reply; // absent on transport/clock/stop; never extrapolated or silently restamped
};
using SafetyTelemetryClock=std::function<GameGuardTime()>;
class SafetyTelemetryOwner {
   public:
    SafetyTelemetryOwner(std::filesystem::path,std::vector<std::string>,SafetyTelemetryPolicy={},SafetyTelemetryClock={});
    ~SafetyTelemetryOwner();
    SafetyTelemetryOwner(const SafetyTelemetryOwner &)=delete;
    SafetyTelemetryOwner &operator=(const SafetyTelemetryOwner &)=delete;
    bool start(uint64_t epoch);
    void requestStop();
    void stop();
    std::shared_ptr<const SafetyTelemetrySnapshot> snapshot()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
