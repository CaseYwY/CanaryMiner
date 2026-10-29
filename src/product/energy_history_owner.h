// Explicit, single-flight helper owner. Admission/tickets are not durable observations or mining permission.
#pragma once
#include "energy_history_process.h"
#include <functional>

namespace om {
enum class EnergyHistoryOwnerState { Stopped, Starting, Ready, Busy, NeedsReview, Faulted };
enum class EnergyHistoryAdmission { Queued, NotReady, StaleGeneration, ReviewChanged, Invalid };
struct EnergyHistoryOwnerSnapshot {
    EnergyHistoryOwnerState state=EnergyHistoryOwnerState::Stopped;
    uint64_t generation=0,revision=0,lastTicket=0;
    bool pending=false,creationOutcomeUnknown=false;
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    std::shared_ptr<const EnergyHistoryBatch> uncertainBatch; // private immutable original input; no automatic replay
    std::optional<EnergyHistoryReply> reply;
};
using EnergyHistoryClock=std::function<int64_t()>; // pure UTC clock only
class EnergyHistoryOwner {
   public:
    EnergyHistoryOwner(std::filesystem::path helper,std::filesystem::path file,EnergyHistoryBinding binding,
                       EnergyHistoryLimits limits={},unsigned timeoutMs=10000,EnergyHistoryClock clock={});
    ~EnergyHistoryOwner();
    EnergyHistoryOwner(const EnergyHistoryOwner &)=delete;
    EnergyHistoryOwner &operator=(const EnergyHistoryOwner &)=delete;
    bool start(uint64_t generation,EnergyHistoryOpen mode=EnergyHistoryOpen::Existing);
    EnergyHistoryAdmission submit(uint64_t generation,uint64_t revision,const EnergyHistoryBatch &batch,const std::vector<EnergyHistoryQuery> &queries={});
    EnergyHistoryAdmission refresh(uint64_t generation,uint64_t revision,const std::vector<EnergyHistoryQuery> &queries={});
    EnergyHistoryAdmission retryReviewed(uint64_t generation,uint64_t revision,const std::vector<EnergyHistoryQuery> &queries={});
    void requestStop(); // memory-only revoke/discard; no disk or process wait
    void stop(); // join after cancellation, outside mining/render/safety paths
    std::shared_ptr<const EnergyHistoryOwnerSnapshot> snapshot() const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace om
