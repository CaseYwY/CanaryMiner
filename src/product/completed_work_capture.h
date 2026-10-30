// Exact private raw-work accumulator. One worker thread, no sampler, transport, storage, profile or financial authority.
#pragma once
#include "core/completed_work.h"
#include "exact_decimal.h"
#include "session_intent.h"
#include <optional>
namespace om {
enum class CompletedWorkState { Dormant, Disabled, Ready, Running, Stopped, Faulted };
enum class CompletedWorkStatus { Missing, Ok, Disabled, Invalid, Clock, Capacity, Attribution, Discontinuity, Stopped };
struct CompletedWorkPolicy {
    bool enabled=false;unsigned maximumRuns=8192;
    std::string boundarySourceKey={}; // empty disables boundary intervals; otherwise an explicitly provisioned private 128-bit hex key
};
struct CompletedWorkOriginal {WorkLane lane=WorkLane::User;uint64_t run=0,number=0;CompletedWorkBatch batch;};
struct CompletedWorkInterval {
    std::string sourceKey;
    uint64_t generation=0,counterEpoch=0,run=0,number=0,beginEvent=0,endEvent=0,batches=0,attempts=0;
    WorkLane lane=WorkLane::User;
    CompletedWorkBoundaryKind kind=CompletedWorkBoundaryKind::Leaving;
    unsigned appliedHostWaitMs=0;
    CompletedWorkTime begin,end;
    ExactDecimal userTmacs,donationTmacs; // constant for this entire interval, not interpolated
};
struct CompletedWorkSnapshot {
    CompletedWorkState state=CompletedWorkState::Dormant;
    CompletedWorkStatus status=CompletedWorkStatus::Missing;
    uint64_t generation=0,counterEpoch=0,runs=0,batches=0,attempts=0;
    uint64_t workUnitsPerAttempt=0,macsPerUnit=0;
    ExactDecimal userTmacs,donationTmacs;
    WorkLane lane=WorkLane::User;
    CompletedWorkTime observed; // latest original lifecycle observation, never a poll time
    std::optional<CompletedWorkOriginal> lastBatch; // historical complete boundary, not a fresh sample during the next search
    uint64_t boundaryEvents=0,intervals=0;
    std::optional<CompletedWorkInterval> lastInterval; // original bracket; end() and snapshot() never extend it
};
// Must outlive every sequential run borrowing it. No concurrent reads; future transport must copy on this thread.
class CompletedWorkCapture final:public CompletedWorkSink {
   public:
    CompletedWorkCapture(CompletedWorkPolicy,const SessionIntent &,SessionToken,uint64_t counterEpoch);
    CompletedWorkCapture(const CompletedWorkCapture &)=delete;
    CompletedWorkCapture &operator=(const CompletedWorkCapture &)=delete;
    bool start(CompletedWorkTime)noexcept;
    bool begin(WorkLane,CompletedWorkTime)noexcept override;
    bool completed(const CompletedWorkBatch &)noexcept override;
    bool boundary(const CompletedWorkBoundary &)noexcept override;
    bool end(bool reconciled,CompletedWorkTime)noexcept override;
    void requestStop()noexcept;
    const CompletedWorkSnapshot &snapshot()const noexcept{return view_;}
   private:
    CompletedWorkPolicy policy_;const SessionIntent &intent_;SessionToken token_;uint64_t counterEpoch_;
    CompletedWorkSnapshot view_;CompletedWorkTime anchor_;bool attempted_=false;
    std::optional<CompletedWorkBoundary> boundary_;uint64_t boundaryEvent_=0;
    bool time(CompletedWorkTime)noexcept;
    bool fail(CompletedWorkStatus)noexcept;
};
} // namespace om
