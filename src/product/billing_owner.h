// Default-off background monetary recovery. Queue admission and stored stage outcomes are not current totals.
#pragma once
#include "billing_input_query.h"
#include <functional>
namespace om {
struct BillingOwnerTime {int64_t utcMs=0;uint64_t monotonicMs=0;};
using BillingOwnerClock=std::function<BillingOwnerTime()>; // pure, nonblocking; serialized by the owner
struct BillingOwnerPolicy {bool enabled=false;unsigned operationTimeoutMs=10000;};
struct BillingOwnerHelpers {std::filesystem::path stageQuery,monetaryQuery,input,retry;};
struct BillingOwnerBinding {std::filesystem::path file;BillingBinding binding;BillingLimits limits;};
enum class BillingOwnerState { Stopped, Busy, Ready, NeedsReview, ClockFault, Faulted };
enum class BillingOwnerAction { Refresh, CreateNew, Submit, Commit, Retry, Reconcile };
enum class BillingOwnerAdmission { Queued, Stopped, NotReady, StaleGeneration, ReviewChanged, Invalid };
struct BillingOwnerView {
    int64_t observedUtcMs=0;
    bool stable=false; // stage -> monetary -> identical stage, not a transaction spanning both stores
    std::shared_ptr<const BillingInputSnapshot> stage;
    std::shared_ptr<const BillingRecoverySnapshot> monetary;
};
struct BillingOwnerIntent {
    BillingOwnerAction action=BillingOwnerAction::Refresh;
    std::optional<BillingInputManifest> manifest;
    std::shared_ptr<const BillingChunk> original;
    std::shared_ptr<const BillingRecoverySnapshot> monetary; // reviewed prior / exact retry original
};
struct BillingOwnerSnapshot {
    BillingOwnerState state=BillingOwnerState::Stopped;
    uint64_t generation=0,revision=0,lastTicket=0,completedTicket=0;
    bool pending=false,reviewable=false;
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    BillingStatus status=BillingStatus::Storage;
    BillingOwnerAction action=BillingOwnerAction::Refresh;
    std::shared_ptr<const BillingOwnerBinding> context;
    std::shared_ptr<const BillingOwnerView> view; // retained historical on stop/failure; only reviewable authorizes admission
    std::shared_ptr<const BillingOwnerIntent> uncertain; // exact private input, never automatically discarded or replayed
    std::shared_ptr<const BillingRecoverySnapshot> actionProof; // historical confirmation, separate from a new readonly view
};
class BillingOwner {
   public:
    BillingOwner(BillingOwnerHelpers helpers,BillingOwnerBinding binding,BillingOwnerPolicy policy={},BillingOwnerClock clock={});
    ~BillingOwner();
    bool start(uint64_t generation); // once per instance; initial readonly inspection only
    BillingOwnerAdmission refresh(uint64_t generation,uint64_t revision);
    BillingOwnerAdmission createReviewed(uint64_t generation,uint64_t revision);
    BillingOwnerAdmission submitReviewed(uint64_t generation,uint64_t revision,const BillingChunk &chunk);
    BillingOwnerAdmission commitReviewed(uint64_t generation,uint64_t revision); // complete Receiving input only
    BillingOwnerAdmission retryReviewed(uint64_t generation,uint64_t revision);
    BillingOwnerAdmission reconcileReviewed(uint64_t generation,uint64_t revision);
    bool invalidate(uint64_t generation);
    void requestStop(); // revoke publication/admission, no I/O/join
    void stop();
    std::shared_ptr<const BillingOwnerSnapshot> snapshot()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
#ifdef CANARY_BILLING_OWNER_TESTING
    friend struct BillingOwnerTestPeer;
#endif
};
#ifdef CANARY_BILLING_OWNER_TESTING
struct BillingOwnerTestPeer {static void unreapedNext(BillingOwner &owner);};
#endif
} // namespace om
