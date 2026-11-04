// Explicit-parent replay of original raw events, never a qualified measurement or current worker permission.
#pragma once
#include "completed_work_capture.h"
#include "worker_work_stream.h"
namespace om {
class WorkerWorkReplay {
   public:
    WorkerWorkReplay(WorkerControlBinding,const SessionIntent &,SessionToken,unsigned maximumRuns=8192);
    WorkerWorkReplay(const WorkerWorkReplay &)=delete;
    WorkerWorkReplay &operator=(const WorkerWorkReplay &)=delete;
    bool accept(const WorkerWorkEvent &)noexcept;
    void requestStop()noexcept;
    bool complete()const noexcept{return finished_&&!failed_;}
    bool failed()const noexcept{return failed_;}
    uint64_t sequence()const noexcept{return sequence_;}
    const CompletedWorkSnapshot &snapshot()const noexcept{return capture_.snapshot();}
   private:
    WorkerControlBinding binding_;const SessionIntent &intent_;SessionToken token_;CompletedWorkCapture capture_;
    CompletedWorkTime anchor_;uint64_t sequence_=0;bool failed_=false,finished_=false;
    bool fail()noexcept;
};
} // namespace om
