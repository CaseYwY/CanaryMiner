// Owned local worker IPC. Launch/reap belong to one background owner; never call launch from a UI/control callback.
#pragma once
#include "worker_control.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>
namespace om {
struct WorkerChannelHandles {uint64_t input=0,output=0,parent=0;}; // inherited handles/descriptors only, not keys
bool parseWorkerChannelHandles(const char *,const char *,const char *,WorkerChannelHandles *);
class WorkerControlClient final:public RunPauseControl {
   public:
    WorkerControlClient();
    ~WorkerControlClient();
    WorkerControlClient(const WorkerControlClient &)=delete;
    WorkerControlClient &operator=(const WorkerControlClient &)=delete;
    bool adopt(WorkerChannelHandles); // single-use; verifies local parent/channel before starting its IO thread
    void requestStop();
    void stop(); // revoke before join; IO never waits for a peer to read/write
    WorkerControlSnapshot snapshot()const;
    bool pauseRequested()const noexcept override;
    WorkControlRequest requestState()const noexcept override;
    void pauseBoundary(bool)noexcept override;
    void pauseHeld()noexcept override;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
enum class WorkerProcessState { Dormant, Running, Stopping, Exited, Failed, Unreaped };
struct WorkerProcessPoll {
    WorkerProcessState state=WorkerProcessState::Dormant;
    int exitCode=-1;
    std::vector<WorkerReceipt> receipts; // at most 16 per poll; private, never log/stats fields
    std::string output; // at most 8192 bytes per poll, separate from the control channel; caller redacts
};
class WorkerControlProcess {
   public:
    WorkerControlProcess();
    ~WorkerControlProcess();
    WorkerControlProcess(const WorkerControlProcess &)=delete;
    WorkerControlProcess &operator=(const WorkerControlProcess &)=delete;
    // One-shot launch. initial must be revision-1 Pause from this session's issuer. No shell or environment key transport.
    bool start(const std::filesystem::path &,const std::vector<std::string> &,const WorkerCommand &initial,const std::atomic<bool> &cancel);
    bool submit(const WorkerCommand &); // one bounded unsent frame; never overwrite a partial write
    WorkerProcessPoll poll(); // bounded nonblocking reads/writes and wait(0), on the owning thread
    void requestStop(); // close command channel; does not claim process exit
    bool terminate(unsigned reapMs=250); // kill only the owned process/group and retain unresolved ownership
    bool reaped()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
