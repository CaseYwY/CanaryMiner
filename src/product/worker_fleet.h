// Optional supervisor-owned control seam. Every callback is memory-only except stop(), which joins after revocation.
#pragma once
#include "game_guard.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
namespace om {
struct WorkerSafetyEvidence {
    uint64_t epoch=0,revision=0;
    GameGuardTime observed;
    GameSafety safety; // aggregate of all frozen selected workers; missing critical reads must be invalid
};
struct WorkerFleetEvidence {
    WorkerSafetyEvidence safety;
    std::shared_ptr<const GameProfitWindow> profit;
};
enum class WorkerFleetState { Dormant, Starting, Active, Stopped, Faulted };
struct WorkerFleetSnapshot {
    uint64_t generation=0,commandRevision=0,commandDeadlineMonoMs=0;
    bool requested=false,done=false,reaped=false,revoked=true,retryable=false,ownershipUnresolved=false;
    bool pauseRequested=true,paused=false; // Paused requires the current drained receipt, not a command or cached stats
    int exitCode=-1;
    GameGuardState game=GameGuardState::Stopped;
};
class WorkerFleet {
   public:
    virtual ~WorkerFleet()=default;
    virtual bool start(unsigned slots,GameGuardTime now)=0;
    virtual bool launch(unsigned slot,std::filesystem::path exe,std::vector<std::string> args,unsigned drainMs,GameGuardTime now)=0;
    virtual void tick(GameGuardTime now,const WorkerFleetEvidence &)=0;
    virtual WorkerFleetState state()const=0;
    virtual WorkerFleetSnapshot snapshot(unsigned slot)const=0;
    virtual std::vector<std::string> takeLines(unsigned slot)=0; // caller redacts; no control keys in these lines
    virtual void requestStop()=0;
    virtual void stop()=0;
};
using WorkerFleetFactory=std::function<std::unique_ptr<WorkerFleet>(const SessionIntent &,SessionToken)>;
using WorkerFleetEvidenceSource=std::function<WorkerFleetEvidence(uint64_t epoch)>; // frozen trusted source, no I/O/automatic timestamp refresh
} // namespace om
