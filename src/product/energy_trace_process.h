// Parent-bound, one-shot readonly committed energy query. No writes, retries, collection or monetary activation.
#pragma once
#include "energy_history_process.h"
namespace om {
constexpr size_t energyTraceRequestLimit=48*1024;
struct EnergyTraceRequest {
    EnergyHistoryRequest source; // Existing + Inspect, exactly one plain half-open query, no batch
    size_t maxIntervals=512;
};
struct EnergyTraceReply {
    EnergyHistoryReply source;
    EnergyHistoryStatus status=EnergyHistoryStatus::Storage;
    std::optional<PowerTrace> completeTrace;
};
struct EnergyTraceProcessResult {
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    std::optional<EnergyTraceReply> reply;
};
bool validEnergyTraceRequest(const EnergyTraceRequest &request);
bool encodeEnergyTraceRequest(const EnergyTraceRequest &request,uint64_t parentPid,std::string *wire);
bool decodeEnergyTraceRequest(const std::string &wire,EnergyTraceRequest *request,uint64_t *parentPid);
bool encodeEnergyTraceReply(const EnergyTraceRequest &request,const EnergyTraceReply &reply,std::string *wire);
bool decodeEnergyTraceReply(const EnergyTraceRequest &request,const std::string &wire,EnergyTraceReply *reply);
EnergyTraceReply executeEnergyTraceRequest(const EnergyTraceRequest &request); // helper-side only
class EnergyTraceProcessClient {
   public:
    explicit EnergyTraceProcessClient(std::filesystem::path helper):process_(std::move(helper)){}
    EnergyTraceProcessResult transact(const EnergyTraceRequest &request,const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
};
} // namespace om
