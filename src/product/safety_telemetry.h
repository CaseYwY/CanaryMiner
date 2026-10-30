// Bounded private GPU-core temperature / clock-event observations. These are not a complete safety permit.
#pragma once
#include "game_guard.h"
#include "local_task_process.h"
#include <optional>
#include <vector>
namespace om {
constexpr size_t safetyTelemetryWireLimit=16384,safetyTelemetryDeviceLimit=64;
enum class SafetyTelemetryStatus { Ok, Disabled, Unavailable, Unsupported, Identity, DeviceFault, Clock, Invalid };
struct SafetyTelemetryRequest {
    bool enabled=false;
    uint64_t epoch=0,revision=0;
    GameGuardTime issued;
    unsigned collectionBudgetMs=500;
    std::vector<std::string> devices; // frozen ordered canonical full UUIDs, private; no index enumeration
};
struct SafetyTelemetryReading {
    int temperatureC=0;
    uint64_t activeClockEvents=0,supportedClockEvents=0;
};
struct SafetyTelemetryReply {
    SafetyTelemetryStatus status=SafetyTelemetryStatus::Unavailable;
    GameGuardTime began,finished; // original clocks span load through unload; failures may record rollback
    std::vector<SafetyTelemetryReading> readings; // all requested devices, or none on any failure
};
GameGuardTime safetyTelemetryTime();
bool validSafetyTelemetryRequest(const SafetyTelemetryRequest &);
bool encodeSafetyTelemetryRequest(const SafetyTelemetryRequest &,uint64_t parentPid,std::string *);
bool decodeSafetyTelemetryRequest(const std::string &,SafetyTelemetryRequest *,uint64_t *parentPid);
bool encodeSafetyTelemetryReply(const SafetyTelemetryRequest &,const SafetyTelemetryReply &,std::string *);
bool decodeSafetyTelemetryReply(const SafetyTelemetryRequest &,const std::string &,SafetyTelemetryReply *);
// Helper-only native entry: runtime loader / NVML operations may block and must remain outside the supervisor.
SafetyTelemetryReply collectSafetyTelemetry(const SafetyTelemetryRequest &);
struct SafetyTelemetryResult {LocalTaskStatus transport=LocalTaskStatus::Unavailable;std::optional<SafetyTelemetryReply> reply;};
class SafetyTelemetryClient {
   public:
    explicit SafetyTelemetryClient(std::filesystem::path helper):process_(std::move(helper)){}
    SafetyTelemetryResult sample(const SafetyTelemetryRequest &,const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
};
} // namespace om
