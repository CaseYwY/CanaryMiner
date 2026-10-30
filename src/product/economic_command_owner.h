// Default-off control-thread command bookkeeping. No process launch, transport, source fetch or ordinary activation.
#pragma once
#include "economic_choice_guard.h"
namespace om {
struct EconomicCommandPolicy {bool enabled=false;EconomicGuardPolicy guard;uint64_t commandLeaseMs=5000;};
enum class EconomicCommandState { Dormant, Disabled, Running, Stopped, ClockFault, Faulted, Retired };
struct EconomicRunBinding {
    WorkerCommand command;
    OptimizationTime selected;
    EconomicGuardSnapshot decision;
    EconomicGuardPolicy policy;
    std::shared_ptr<const EconomicStudy> study; // exact immutable full profile, scoring inputs and source identities
    uint64_t safetyGameDeadlineMonoMs=0;
};
struct EconomicCommandSnapshot {
    EconomicCommandState state=EconomicCommandState::Dormant;
    bool pending=false,revokeRequired=false;
    uint64_t commandRevision=0;
    std::shared_ptr<const EconomicRunBinding> lastRun; // historical original, never standalone permission
    std::optional<WorkerReceipt> application; // WaitApplied for exactly lastRun, not Accepted
};
// All methods and borrowed objects belong to one control thread. This is the sole issuer/acknowledgement caller
// after the initial Pause. Poll the evaluator's current context/conditions first. The parent budget outlives this owner.
class EconomicCommandOwner {
   public:
    EconomicCommandOwner(EconomicCommandPolicy,WorkerControlBinding,const EconomicEvaluationOwner &,WorkerControlIssuer &,
        EconomicParentBudget &,const SessionIntent &,SessionToken);
    ~EconomicCommandOwner();
    EconomicCommandOwner(const EconomicCommandOwner &)=delete;
    EconomicCommandOwner &operator=(const EconomicCommandOwner &)=delete;
    bool start(OptimizationTime,const WorkProfilePlan &);
    // The permission ceiling is the current complete safety/game bound, not early-Pause delivery headroom.
    // Return each new command once. The enclosing transport must submit it or call transportFailed, and revoke
    // its channel immediately when revokeRequired is true. This object cannot prove channel closure or reaping.
    std::optional<WorkerCommand> poll(OptimizationTime,uint64_t contextRevision,const WorkProfilePlan &,const WorkProfileConditions &,uint64_t permissionUntilMonoMs);
    WorkerReceiptResult acknowledge(const WorkerReceipt &,OptimizationTime);
    void transportFailed();
    void requestStop();
    bool retire(OptimizationTime); // stopped/settled issuer only; process exit/retry admission remains external
    const EconomicCommandSnapshot &snapshot()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
