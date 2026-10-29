// Single-owner, memory-only scheduling requests. A ticket is not mining, network or measurement permission.
#pragma once
#include "session_intent.h"
#include <array>
#include <cstddef>
#include <optional>
namespace om {
struct OptimizationTime {int64_t utcMs=0;uint64_t monotonicMs=0;};
enum class OptimizationLane { Market, Evaluation, RetestReview };
enum class OptimizationScheduleState { Dormant, Disabled, Running, Stopped, ClockFault, Faulted };
enum class OptimizationOutcome { Complete, Retryable, Rejected, Cancelled, Unreaped };
enum class OptimizationCompletion { Accepted, Expired, WrongTicket, Stopped, Faulted };
struct OptimizationSchedulePolicy {
    bool enabled=false;
    uint64_t marketRefreshMs=3600000,evaluationMs=3600000,retestReviewMs=0;
    uint64_t retryMs=30000,operationTimeoutMs=10000;
    unsigned maximumRetries=3;
};
bool validOptimizationSchedulePolicy(const OptimizationSchedulePolicy &policy);
struct OptimizationTicket {
    uint64_t generation=0,sequence=0;
    OptimizationLane lane=OptimizationLane::Market;
    unsigned attempt=0; // 0 original; subsequent bounded retries
    OptimizationTime issued;
    uint64_t deadlineMonoMs=0;
};
struct OptimizationLaneState {
    bool enabled=false,cancellationRequested=false;
    uint64_t dueMonoMs=0;
    unsigned retriesUsed=0;
    std::optional<OptimizationTicket> pending;
    std::optional<OptimizationOutcome> lastOutcome;
    bool lastExpired=false;
};
struct OptimizationScheduleSnapshot {
    OptimizationScheduleState state=OptimizationScheduleState::Dormant;
    uint64_t generation=0;
    bool unreaped=false;
    std::array<OptimizationLaneState,3> lanes;
};
struct OptimizationSchedulePoll {
    OptimizationScheduleState state=OptimizationScheduleState::Dormant;
    std::array<std::optional<OptimizationTicket>,3> requests,cancellations;
};
class OptimizationSchedule {
   public:
    OptimizationSchedule(OptimizationSchedulePolicy policy,const SessionIntent &intent,SessionToken token):policy_(policy),intent_(intent),token_(token){}
    OptimizationSchedule(const OptimizationSchedule &)=delete;
    OptimizationSchedule &operator=(const OptimizationSchedule &)=delete;
    bool start(OptimizationTime now); // single-use; caller must already own an explicit session token
    OptimizationSchedulePoll poll(OptimizationTime now);
    // Settles ownership even after expiry. Expired/Cancelled work cannot publish a result; an uncompleted slot is never replaced.
    OptimizationCompletion complete(const OptimizationTicket &ticket,OptimizationOutcome outcome,OptimizationTime now);
    void requestStop();
    const OptimizationScheduleSnapshot &snapshot()const{return snapshot_;}
   private:
    OptimizationSchedulePolicy policy_;const SessionIntent &intent_;SessionToken token_;
    OptimizationScheduleSnapshot snapshot_;
    bool attempted_=false;
    uint64_t sequence_=0;
    OptimizationTime anchor_,last_;
    std::array<uint64_t,3> regularDue_{};
    std::array<bool,3> cancellationDelivered_{};
    bool time(OptimizationTime now);
    bool later(uint64_t now,uint64_t delay,uint64_t &out);
    uint64_t interval(size_t lane)const;
    void halt(OptimizationScheduleState state);
    void expire(OptimizationTime now);
    void reschedule(size_t lane,OptimizationTime now,bool retry);
};
} // namespace om
