// Private lossless raw-work events. No sensor, profile qualification, command or financial authority.
#pragma once
#include "core/completed_work.h"
#include "worker_control.h"
#include <functional>
namespace om {
enum class WorkerWorkKind { Start, Begin, Completed, Paused, Runnable, Leaving, End, Finish };
struct WorkerWorkEvent {
    WorkerControlBinding binding;
    uint64_t sequence=0;
    WorkerWorkKind kind=WorkerWorkKind::Start;
    CompletedWorkTime observed;
    WorkLane lane=WorkLane::User; // Begin only; every unused payload field must be canonical zero/User
    uint64_t workUnits=0,attempts=0,macsPerUnit=0;
    unsigned appliedHostWaitMs=0;
    bool flag=false; // Completed: verification failed; End: reconciled
};
bool validWorkerWorkEvent(const WorkerWorkEvent &);
bool encodeWorkerWorkEvent(const WorkerWorkEvent &,std::string *);
bool decodeWorkerWorkEvent(std::string_view,WorkerWorkEvent *);
bool sameWorkerWorkBinding(const WorkerControlBinding &,const WorkerControlBinding &);
// Exactly one worker thread; callback must be bounded memory-only. Queue refusal is terminal, never a skipped event.
class WorkerWorkPublisher final:public CompletedWorkSink {
   public:
    using Publish=std::function<bool(const WorkerWorkEvent &)>;
    WorkerWorkPublisher(WorkerControlBinding,Publish);
    WorkerWorkPublisher(const WorkerWorkPublisher &)=delete;
    WorkerWorkPublisher &operator=(const WorkerWorkPublisher &)=delete;
    bool start(CompletedWorkTime)noexcept;
    bool begin(WorkLane,CompletedWorkTime)noexcept override;
    bool completed(const CompletedWorkBatch &)noexcept override;
    bool boundary(const CompletedWorkBoundary &)noexcept override;
    bool end(bool reconciled,CompletedWorkTime)noexcept override;
    bool finish(CompletedWorkTime)noexcept;
    bool failed()const noexcept{return failed_;}
   private:
    WorkerControlBinding binding_;Publish publish_;uint64_t sequence_=0;
    bool started_=false,running_=false,finished_=false,failed_=false;
    bool emit(WorkerWorkEvent)noexcept;
};
} // namespace om
