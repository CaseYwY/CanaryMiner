#pragma once
#include "energy_ledger.h"
#include "local_task_process.h"

namespace om {
struct EnergyHistoryQuery {
    int64_t beginUtcMs=0,endUtcMs=0;
    std::string registerId,periodId; // optional exact-window assertion; mismatch rejects instead of choosing another period
};
struct EnergyHistoryRequest {
    uint64_t generation=0,ticket=0;
    std::filesystem::path file;
    EnergyHistoryBinding binding;
    EnergyHistoryLimits limits;
    EnergyHistoryOpen mode=EnergyHistoryOpen::Existing;
    int64_t nowUtcMs=0;
    std::optional<EnergyHistoryBatch> batch;
    std::vector<EnergyHistoryQuery> queries;
};
struct EnergyHistoryReply {
    uint64_t generation=0,ticket=0;
    EnergyHistoryStatus opened=EnergyHistoryStatus::Storage;
    std::optional<EnergyHistoryStatus> applied;
    EnergyHistoryCheckpoint checkpoint;
    std::vector<EnergyHistoryTotals> totals;
};
struct EnergyHistoryProcessResult {
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    bool writeOutcomeUnknown=false;
    std::optional<EnergyHistoryReply> reply;
};
bool encodeEnergyHistoryRequest(const EnergyHistoryRequest &request,std::string *wire);
bool decodeEnergyHistoryRequest(const std::string &wire,EnergyHistoryRequest *request);
bool encodeEnergyHistoryReply(const EnergyHistoryRequest &request,const EnergyHistoryReply &reply,std::string *wire);
bool decodeEnergyHistoryReply(const EnergyHistoryRequest &request,const std::string &wire,EnergyHistoryReply *reply);
EnergyHistoryReply executeEnergyHistoryRequest(const EnergyHistoryRequest &request); // helper-side synchronous I/O only
class EnergyHistoryProcessClient {
   public:
    explicit EnergyHistoryProcessClient(std::filesystem::path helper):process_(std::move(helper)){}
    EnergyHistoryProcessResult transact(const EnergyHistoryRequest &request,const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
};
} // namespace om
