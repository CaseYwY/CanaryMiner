// Owned local worker IPC. Launch/reap belong to one background owner; never call launch from a UI/control callback.
#pragma once
#include "worker_control.h"
#include "worker_work_stream.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>
namespace om {
struct WorkerChannelHandles {uint64_t input=0,output=0,parent=0;}; // inherited handles/descriptors only, not keys
bool parseWorkerChannelHandles(const char *,const char *,const char *,WorkerChannelHandles *);
class WorkerControlClient final:public RunPauseControl {
   public:
    explicit WorkerControlClient(bool workStream=false); // explicitly opt in before adoption; ordinary callers remain off
    ~WorkerControlClient();
    WorkerControlClient(const WorkerControlClient &)=delete;
    WorkerControlClient &operator=(const WorkerControlClient &)=delete;
    bool adopt(WorkerChannelHandles); // single-use; verifies local parent/channel before starting its IO thread
    void requestStop();
    void stop(); // revoke before join; IO never waits for a peer to read/write
    WorkerControlSnapshot snapshot()const;
    bool publishWork(const WorkerWorkEvent &)noexcept; // one worker producer, bounded memory only; refusal must stop capture
    uint64_t flushedWorkSequence()const; // local complete-write progress only, NOT peer consumption or a control receipt
    bool pauseRequested()const noexcept override;
    WorkControlRequest requestState()const noexcept override;
    void pauseBoundary(bool)noexcept override;
    void pauseHeld()noexcept override;
    WorkControlRequest applyWorkWait(unsigned &)noexcept override;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
enum class WorkerProcessState { Dormant, Running, Stopping, Exited, Disconnected, Failed, Unreaped };
struct WorkerProcessPoll {
    WorkerProcessState state=WorkerProcessState::Dormant;
    int exitCode=-1;
    bool controlFault=false; // survives a simultaneous actual exit observation
    std::vector<WorkerReceipt> receipts; // at most 16 per poll; private, never log/stats fields
    std::vector<WorkerWorkEvent> work; // default-off original raw events, not qualified samples or control receipts
    bool workStreamClosed=false,workStreamComplete=false; // complete only after Finish AND a clean drained EOF
    std::string output; // at most 8192 bytes per poll, separate from the control channel; caller redacts
};
class WorkerControlProcess {
   public:
    WorkerControlProcess();
    ~WorkerControlProcess();
    WorkerControlProcess(const WorkerControlProcess &)=delete;
    WorkerControlProcess &operator=(const WorkerControlProcess &)=delete;
    // One-shot launch. initial must be revision-1 Pause from this session's issuer. No shell or environment key transport.
    bool start(const std::filesystem::path &,const std::vector<std::string> &,const WorkerCommand &initial,const std::atomic<bool> &cancel,bool workStream=false);
    bool submit(const WorkerCommand &); // one bounded unsent frame; never overwrite a partial write
    WorkerProcessPoll poll(); // bounded nonblocking reads/writes and wait(0), on the owning thread
    void requestStop(); // close command channel; does not claim process exit
    bool terminate(unsigned reapMs=250); // kill only the owned process/group and retain unresolved ownership
    bool reaped()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
