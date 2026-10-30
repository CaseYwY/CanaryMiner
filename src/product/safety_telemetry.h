// Bounded private native observations. Neither collection profile is a complete safety permit.
#pragma once
#include "game_guard.h"
#include "local_task_process.h"
#include <array>
#include <optional>
#include <vector>
namespace om {
constexpr size_t safetyTelemetryWireLimit=16384,safetyTelemetryDeviceLimit=64;
constexpr size_t safetyTelemetryExtendedWireLimit=32768;
enum class SafetyTelemetryProfile { CoreEvents, ExtendedFields };
// NVML field IDs: memory temperature; shutdown/slowdown/memory/GPU T.Limits; recovery action.
// T.Limits retain their native meaning; they are NOT absolute operating-temperature defaults.
constexpr std::array<unsigned,6> safetyTelemetryFieldIds{{82,193,194,195,196,230}};
enum class SafetyTelemetryStatus { Ok, Disabled, Unavailable, Unsupported, Identity, DeviceFault, Clock, Invalid };
struct SafetyTelemetryRequest {
    bool enabled=false;
    uint64_t epoch=0,revision=0;
    GameGuardTime issued;
    unsigned collectionBudgetMs=500;
    std::vector<std::string> devices; // frozen ordered canonical full UUIDs, private; no index enumeration
    SafetyTelemetryProfile profile=SafetyTelemetryProfile::CoreEvents;
};
struct SafetyTelemetryField {
    unsigned id=0,valueType=0; // original NVML integral type, never coerced from a floating-point value
    int64_t timestampUsec=0,latencyUsec=0,value=0; // original CPU UTC timestamp, update latency, exact integer
};
struct SafetyTelemetryReading {
    int temperatureC=0;
    uint64_t activeClockEvents=0,supportedClockEvents=0;
    std::vector<SafetyTelemetryField> fields{}; // empty for CoreEvents; all six ordered fields for ExtendedFields
};
struct SafetyTelemetryReply {
    SafetyTelemetryStatus status=SafetyTelemetryStatus::Unavailable;
    GameGuardTime began,finished; // original clocks span load through unload; failures may record rollback
    std::vector<SafetyTelemetryReading> readings; // all requested devices, or none on any failure
};
GameGuardTime safetyTelemetryTime();
bool validSafetyTelemetryRequest(const SafetyTelemetryRequest &);
bool validSafetyTelemetryField(const SafetyTelemetryField &,size_t index,GameGuardTime began,GameGuardTime finished,unsigned budgetMs);
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
