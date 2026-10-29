// Default-off readonly source notifications and exact candidate computation. No monetary write authority.
#pragma once
#include "billing_owner.h"
namespace om {
struct BillingSourceNotice {
    EnergyHistoryAnchor checkpoint; // a scheduling hint, NEVER proof of persisted source facts
    int64_t throughUtcMs=0,observedUtcMs=0;
};
struct BillingComputePolicy {bool enabled=false;unsigned operationTimeoutMs=10000;size_t maxIntervals=512;};
enum class BillingComputeState { Stopped, Waiting, Busy, Ready, Unavailable, ClockFault, Faulted };
enum class BillingComputeAdmission { Queued, Duplicate, Stopped, StaleGeneration, ReviewChanged, NotReady, Invalid };
struct BillingCandidate {
    std::shared_ptr<const BillingOwnerSnapshot> reviewed; // immutable exact monetary version; not refreshed by source reads
    BillingSourceNotice notice;
    BillingChunk original;
    BillAccrual preview; // hypothetical next prefix, NOT committed money or current income
    int64_t queriedUtcMs=0;
};
struct BillingComputeSnapshot {
    BillingComputeState state=BillingComputeState::Stopped;
    uint64_t generation=0,revision=0,lastTicket=0,completedTicket=0;
    bool pending=false,reviewable=false;
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    BillingStatus status=BillingStatus::Storage;
    std::optional<EnergyHistoryStatus> sourceStatus;
    std::optional<EnergyHistoryCoverage> sourceCoverage;
    std::optional<ElectricityStatus> calculation;
    std::shared_ptr<const BillingOwnerSnapshot> monetary;
    std::shared_ptr<const BillingCandidate> candidate; // retained historical unless reviewable
};
class BillingComputeOwner {
   public:
    BillingComputeOwner(std::filesystem::path installedTraceHelper,std::shared_ptr<const BillingOwnerBinding> context,
                        BillingComputePolicy policy={},BillingOwnerClock clock={});
    ~BillingComputeOwner();
    bool start(uint64_t generation); // once per lifetime; no startup query
    bool observe(uint64_t generation,std::shared_ptr<const BillingOwnerSnapshot> monetary); // memory-only; replacement revokes candidates
    BillingComputeAdmission notify(uint64_t generation,uint64_t revision,const BillingSourceNotice &notice);
    BillingComputeAdmission refresh(uint64_t generation,uint64_t revision); // explicit retry of last notice, not monetary replay
    // Consume this exact review once at a separate monetary owner's final admission. No writes or callbacks.
    bool claimReviewed(uint64_t generation,uint64_t revision,uint64_t monetaryGeneration,uint64_t monetaryRevision);
    bool invalidate(uint64_t generation);
    void requestStop();
    void stop();
    std::shared_ptr<const BillingComputeSnapshot> snapshot()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
#ifdef CANARY_BILLING_COMPUTE_TESTING
    friend struct BillingComputeTestPeer;
#endif
};
#ifdef CANARY_BILLING_COMPUTE_TESTING
struct BillingComputeTestPeer {static void unreapedNext(BillingComputeOwner &owner);};
#endif
}
