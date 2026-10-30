// Single-incarnation reference guard. Borrows actual evaluator/issuer lifetimes; never issues or sends a command.
#pragma once
#include "economic_choice.h"
#include "economic_evaluation_owner.h"
#include "worker_control.h"
namespace om {
struct EconomicGuardPolicy {
    bool enabled=false;
    EconomicChoicePolicy margins;
    EconomicUsePolicy result;
    uint64_t confirmationMs=10000,maximumConfirmationGapMs=60000,cooldownMs=30000,transitionWindowMs=3600000;
    unsigned minimumEvaluations=2,maximumTransitions=6;
};
enum class EconomicGuardState { Dormant, Disabled, Running, Stopped, ClockFault, Faulted };
enum class EconomicGuardAction { None, Pause, Hold, Resume, Switch };
enum class EconomicGuardReason { Missing, Pending, UnknownApplication, PostApplicationStudy, Unsafe, NoPositive, Ineligible, Margin, Confirming, Cooldown, Budget, Ready, Terminal };
struct EconomicGuardSnapshot {
    EconomicGuardState state=EconomicGuardState::Dormant;
    EconomicGuardAction action=EconomicGuardAction::None;
    EconomicGuardReason reason=EconomicGuardReason::Missing;
    EconomicChoiceState arithmetic=EconomicChoiceState::Unavailable;
    uint64_t contextRevision=0,resultSequence=0,workerRevision=0,validUntilMonoMs=0;
    std::optional<size_t> point;std::optional<unsigned> hostWaitMs;
    unsigned confirmations=0,transitionsInWindow=0;
    bool referenceOnly=true;
};
bool validEconomicGuardPolicy(const EconomicGuardPolicy &);
// All methods and both borrowed owners belong to one control thread and the SAME intent object/token.
// Update evaluator context/conditions before check. Start requires revision-1 proven Pause.
// The frozen worker/plan cannot be rebound; an enclosing parent must preserve budgets across replacement workers.
class EconomicChoiceGuard {
   public:
    EconomicChoiceGuard(EconomicGuardPolicy,WorkerControlBinding,const EconomicEvaluationOwner &,WorkerControlIssuer &,const SessionIntent &,SessionToken);
    ~EconomicChoiceGuard();
    EconomicChoiceGuard(const EconomicChoiceGuard &)=delete;
    EconomicChoiceGuard &operator=(const EconomicChoiceGuard &)=delete;
    bool start(OptimizationTime,const WorkProfilePlan &);
    const EconomicGuardSnapshot &check(OptimizationTime,uint64_t contextRevision,const WorkProfilePlan &,const WorkProfileConditions &);
    void requestStop(); // withdraw reference only; enclosing lifetime still owns worker/evaluator cancellation and reaping
    const EconomicGuardSnapshot &snapshot()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
