// Memory-only control-thread facade over one owned worker lifetime. No automatic command renewal or session start.
#pragma once
#include "worker_process.h"
#include <deque>
namespace om {
enum class WorkerOwnerState { Dormant, Starting, Running, Stopping, Exited, Faulted, Unreaped };
struct WorkerOwnerSnapshot {
    WorkerOwnerState state=WorkerOwnerState::Dormant;
    bool done=false,reaped=false,revoked=false;
    int exitCode=-1;
    uint64_t droppedLines=0;
    std::optional<WorkerReceipt> receipt; // original historical observation; no authority after revocation
};
class WorkerProcessOwner {
   public:
    WorkerProcessOwner();
    ~WorkerProcessOwner();
    WorkerProcessOwner(const WorkerProcessOwner &)=delete;
    WorkerProcessOwner &operator=(const WorkerProcessOwner &)=delete;
    bool start(std::filesystem::path,std::vector<std::string>,WorkerCommand initial,unsigned drainMs=10000);
    bool submit(const WorkerCommand &); // one memory slot, not a received/drained acknowledgement
    WorkerOwnerSnapshot snapshot()const;
    std::vector<std::string> takeLines(); // <=32 complete lines / 32768 bytes; caller must redact
    void requestStop(); // terminal, memory-only; no new launch/command may follow
    void stop(); // revoke, then join outside latency-sensitive control callbacks
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
