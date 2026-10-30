// Raw synchronized/reconciled work, separate from display rates, shares, power and earnings.
#pragma once
#include <chrono>
#include <cstdint>
namespace om {
enum class WorkLane { User, Donation };
struct CompletedWorkTime {int64_t utcMs=0;uint64_t monotonicMs=0;};
struct CompletedWorkBatch {
    CompletedWorkTime observed;
    uint64_t workUnits=0,attempts=0,macsPerUnit=0;
    unsigned appliedHostWaitMs=0;
    bool hostVerificationFailed=false;
};
enum class CompletedWorkBoundaryKind { Paused, Runnable, Leaving };
struct CompletedWorkBoundary {
    CompletedWorkTime observed;
    CompletedWorkBoundaryKind kind=CompletedWorkBoundaryKind::Leaving;
    unsigned appliedHostWaitMs=0;
};
// Borrowed by exactly one worker thread across sequential user/donation runMiner calls. Callbacks must be bounded,
// memory-only and noexcept. false stops the opted-in run; an unsuccessful run end invalidates continuity.
class CompletedWorkSink {
   public:
    virtual ~CompletedWorkSink()=default;
    virtual bool begin(WorkLane,CompletedWorkTime)noexcept=0;
    virtual bool completed(const CompletedWorkBatch &)noexcept=0;
    // Checkpoints on the worker thread only. Leaving closes a known no-work interval BEFORE setup/fetch/prepare/search
    // or wait installation; Paused follows drain and acknowledgement. A cached batch is never a checkpoint.
    virtual bool boundary(const CompletedWorkBoundary &)noexcept{return true;}
    virtual bool end(bool reconciled,CompletedWorkTime)noexcept=0;
};
inline CompletedWorkTime completedWorkNow(){return {
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())};}
// Every early/exceptional exit invalidates the source. Successful closure is explicit, after the final drain.
class CompletedWorkRun {
   public:
    explicit CompletedWorkRun(CompletedWorkSink *sink):sink_(sink){}
    ~CompletedWorkRun(){if(active_)sink_->end(false,completedWorkNow());}
    CompletedWorkRun(const CompletedWorkRun &)=delete;
    CompletedWorkRun &operator=(const CompletedWorkRun &)=delete;
    bool begin(WorkLane lane){if(!sink_)return true;active_=sink_->begin(lane,completedWorkNow());return active_;}
    bool finish(){if(!sink_)return true;if(!active_)return false;active_=false;return sink_->end(true,completedWorkNow());}
    bool boundary(CompletedWorkBoundaryKind kind,unsigned wait){return !sink_||(active_&&sink_->boundary({completedWorkNow(),kind,wait}));}
   private:
    CompletedWorkSink *sink_;bool active_=false;
};
} // namespace om
