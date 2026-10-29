// Bounded producer bridge. Authorization, private namespace and a fresh parent identity are explicit inputs.
#pragma once
#include "energy_history_owner.h"
#include "energy_owner.h"

namespace om {
struct EnergyCapturePolicy {
    bool persistenceAuthorized=false; // independent of in-memory session collection
    unsigned selectionMs=1000,maximumGapMs=5000,flushMs=2000;
    size_t bufferedReadings=128,batchReadings=128;
};
struct EnergyCapturePlan {
    EnergyCapturePolicy policy;
    EnergyHistoryBinding binding;
    EnergyHistoryLimits limits;
    std::string privateNamespace,sessionId; // caller-provisioned; never ordinal, generation, account or worker name
    EnergyHistoryOpen mode=EnergyHistoryOpen::Existing;
};
// Pure mapping: namespace-salted opaque keys, sorted by key, stable when selection order changes.
// The caller must privately retain the namespace and history lower bound, and explicitly bind each selected set.
bool makeEnergyCaptureBinding(const std::string &privateNamespace,int64_t historyBeginUtcMs,
                              const std::vector<std::string> &deviceIds,PowerOrigin origin,EnergyHistoryBinding *out);
enum class EnergyCaptureState { Stopped, Starting, Collecting, NeedsReview, Faulted, ClockFault };
enum class EnergyCaptureAdmission { Buffered, Skipped, Full, NotReady, StaleGeneration, Invalid, Stopped };
struct EnergyCaptureSnapshot {
    EnergyCaptureState state=EnergyCaptureState::Stopped;
    uint64_t generation=0,revision=0,buffered=0,dropped=0,selectedOut=0,continuity=0;
    EnergyHistoryBinding binding; // private; UI hides by default, never forwarded to public stats/logs
    std::shared_ptr<const EnergyHistoryOwnerSnapshot> owner;
    std::shared_ptr<const EnergyHistoryOwnerSnapshot> reconciled; // one confirmed reviewed receipt, retained across later capture jobs
};
class EnergyCaptureSink {
   public:
    virtual ~EnergyCaptureSink()=default;
    virtual bool start(uint64_t generation,const std::vector<std::string> &selectedDeviceIds)=0;
    virtual EnergyCaptureAdmission submit(uint64_t generation,const SessionPowerInput &input)=0;
    virtual void markGap(uint64_t generation)=0; // e.g. worker exit; conservative break across all bound channels
    virtual void tick(int64_t utcMs,uint64_t monotonicMs)=0; // memory-only; never waits for a helper or storage
    virtual EnergyHistoryAdmission inspect(uint64_t generation,uint64_t ownerRevision)=0;
    virtual EnergyHistoryAdmission retryReviewed(uint64_t generation,uint64_t ownerRevision)=0;
    virtual void requestStop()=0;
    virtual void stop()=0;
    virtual std::shared_ptr<const EnergyCaptureSnapshot> snapshot()const=0;
};
using EnergyCaptureFactory=std::function<std::unique_ptr<EnergyCaptureSink>()>;
std::unique_ptr<EnergyCaptureSink> makeEnergyCapture(std::filesystem::path helper,std::filesystem::path file,
                                                  EnergyCapturePlan plan,unsigned timeoutMs=10000,EnergyHistoryClock clock={});
} // namespace om
