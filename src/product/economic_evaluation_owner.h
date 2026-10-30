// Control-thread coordinator plus one owned CPU evaluator. No source fetch, hardware access or mining authority.
#pragma once
#include "economic_study_use.h"
namespace om {
struct EconomicEvaluationPolicy {
    bool enabled=false;
    OptimizationSchedulePolicy schedule;
    EconomicUsePolicy result;
};
enum class EconomicEvaluationState { Dormant, Disabled, Running, Stopped, ClockFault, Faulted };
enum class EconomicSubmission { Accepted, Busy, WrongTicket, WrongContext, Invalid, Expired, Inactive };
struct EconomicEvaluationSnapshot {
    EconomicEvaluationState state=EconomicEvaluationState::Dormant;
    uint64_t contextRevision=0,resultSequence=0;
    // Metadata for the previous immutable result during a safe replacement; cleared on any cancellation/revocation.
    uint64_t retainedResultSequence=0,retainedValidUntilMonoMs=0;
    bool pending=false,cancellationRequested=false;
    EconomicStatus computation=EconomicStatus::Unavailable;
    EconomicUseSnapshot use;
    std::shared_ptr<const EconomicStudy> study; // present only after current control-thread admission
};
// All public methods, the borrowed intent and its lifetime belong to ONE control thread.
// The CPU thread sees only a moved frozen input and independent atomic cancellation, never SessionIntent.
class EconomicEvaluationOwner {
   public:
    EconomicEvaluationOwner(EconomicEvaluationPolicy,const SessionIntent &,SessionToken);
    ~EconomicEvaluationOwner();
    EconomicEvaluationOwner(const EconomicEvaluationOwner &)=delete;
    EconomicEvaluationOwner &operator=(const EconomicEvaluationOwner &)=delete;
    bool start(OptimizationTime);
    bool boundTo(const SessionIntent &,SessionToken)const; // exact borrowed parent object and explicit epoch
    // Revision must advance for ANY changed scoring input/source/configuration, not just work-plan edits.
    // Returned Market/RetestReview tickets require separate permitted owners; no request is executed by poll.
    OptimizationSchedulePoll poll(OptimizationTime,uint64_t contextRevision,const WorkProfilePlan &,const WorkProfileConditions &);
    EconomicSubmission submit(const OptimizationTicket &,uint64_t contextRevision,EconomicInput,OptimizationTime);
    // Evaluation may be declined before submit, but cannot be externally marked Complete or settled while owned.
    OptimizationCompletion completeExternal(const OptimizationTicket &,OptimizationOutcome,OptimizationTime);
    void requestStop(); // revoke now, no join; pending remains true until actual completion/join
    void stop(); // revoke, join CPU thread, settle its ticket; external ownership remains visible
    const EconomicEvaluationSnapshot &snapshot()const;
    const OptimizationScheduleSnapshot &schedule()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
