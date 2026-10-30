// Private bounded worker command/receipt protocol. No process launch, game policy, device, socket or settings activation.
#pragma once
#include "../core/work_pause.h"
#include "session_intent.h"
#include <array>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
namespace om {
constexpr size_t workerControlFrameLimit=384,workerControlChunkLimit=4096,workerControlFramesPerChunk=16;
constexpr uint64_t workerControlMaxLeaseMs=5000;
struct WorkerControlBinding {
    std::string sessionKey,workerKey; // independently provisioned 128-bit lowercase hex; private, never stats/log fields
    uint64_t epoch=0,workerGeneration=0;
    unsigned slot=0;
};
enum class WorkerDirective { Pause, Run };
struct WorkerCommand {
    WorkerControlBinding binding;
    uint64_t revision=0,issuedMonoMs=0,deadlineMonoMs=0;
    WorkerDirective directive=WorkerDirective::Pause;
};
enum class WorkerReceiptKind { Accepted, Paused }; // Accepted never proves quiescence or that a search started
struct WorkerReceipt {WorkerCommand command;WorkerReceiptKind kind=WorkerReceiptKind::Accepted;uint64_t observedMonoMs=0;};
bool validWorkerControlBinding(const WorkerControlBinding &);
bool validWorkerCommand(const WorkerCommand &);
bool sameWorkerCommand(const WorkerCommand &,const WorkerCommand &);
bool encodeWorkerCommand(const WorkerCommand &,std::string *);
bool decodeWorkerCommand(std::string_view,WorkerCommand *);
bool encodeWorkerReceipt(const WorkerReceipt &,std::string *);
bool decodeWorkerReceipt(std::string_view,WorkerReceipt *);
class WorkerControlFramer {
   public:
    bool feed(std::string_view bytes,const std::function<bool(std::string_view)> &frame);
    bool complete()const{return !failed_&&!used_;}
    bool failed()const{return failed_;}
   private:
    std::array<char,workerControlFrameLimit> buffer_{};size_t used_=0;bool failed_=false;
};
enum class WorkerControlState { Waiting, Active, Stopped, Expired, ClockFault, Faulted };
enum class WorkerCommandResult { Accepted, Duplicate, Rejected };
struct WorkerControlSnapshot {
    WorkerControlState state=WorkerControlState::Waiting;
    bool atPausedBoundary=false;
    std::optional<WorkerReceipt> receipt; // original observation time; copies are historical, never transferable authority
};
using WorkerControlClock=std::function<uint64_t()>; // bounded memory/monotonic clock only; called under the state lock
uint64_t workerControlNowMs();
class WorkerControlGate final:public RunPauseControl {
   public:
    explicit WorkerControlGate(WorkerControlBinding,WorkerControlClock={});
    WorkerControlGate(const WorkerControlGate &)=delete;
    WorkerControlGate &operator=(const WorkerControlGate &)=delete;
    WorkerCommandResult receive(const WorkerCommand &);
    WorkerCommandResult receiveFrame(std::string_view);
    void requestStop(); // EOF/operator stop; terminal and higher precedence than a protocol/clock fault
    void fail(); // malformed/overflow/transport failure; terminal, never an implicit resume
    WorkerControlSnapshot snapshot()const;
    bool pauseRequested()const noexcept override;
    WorkControlRequest requestState()const noexcept override;
    void pauseBoundary(bool paused)noexcept override;
    void pauseHeld()noexcept override;
   private:
    const WorkerControlBinding binding_;WorkerControlClock clock_;mutable std::mutex mutex_;
    mutable WorkerControlSnapshot view_;mutable uint64_t lastMonoMs_=0;
    uint64_t startupDeadline_=0;bool confirmedInitialPause_=false;
    bool time()const;void confirmPause();
};
enum class WorkerReceiptResult { Settled, Waiting, Duplicate, Rejected, Terminal };
struct WorkerControlIssuerSnapshot {
    WorkerControlState state=WorkerControlState::Waiting;
    std::optional<WorkerCommand> pending;
    std::optional<WorkerReceipt> settled;
};
// Single parent control thread. The borrowed intent must outlive this issuer; keep it across every command of this worker incarnation.
class WorkerControlIssuer {
   public:
    WorkerControlIssuer(WorkerControlBinding,const SessionIntent &,SessionToken);
    WorkerControlIssuer(const WorkerControlIssuer &)=delete;
    WorkerControlIssuer &operator=(const WorkerControlIssuer &)=delete;
    bool start(uint64_t nowMonoMs);
    std::optional<WorkerCommand> issue(WorkerDirective,uint64_t nowMonoMs,uint64_t leaseMs);
    WorkerReceiptResult acknowledge(const WorkerReceipt &,uint64_t nowMonoMs);
    WorkerControlIssuerSnapshot snapshot(uint64_t nowMonoMs);
    bool paused(uint64_t nowMonoMs); // current matching proof only; issue/revoke/expiry immediately invalidates it
    void requestStop();
   private:
    const WorkerControlBinding binding_;const SessionIntent &intent_;const SessionToken token_;
    WorkerControlIssuerSnapshot view_;bool attempted_=false,initialPauseConfirmed_=false;
    uint64_t revision_=0,lastMonoMs_=0,deadline_=0;
    bool time(uint64_t now);void fault();
};
} // namespace om
