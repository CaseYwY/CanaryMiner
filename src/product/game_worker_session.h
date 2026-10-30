// One fixed game runtime and private parent identity across every admitted worker incarnation.
#pragma once
#include "worker_fleet.h"
#include "game_runtime.h"
namespace om {
struct GameWorkerPolicy {
    unsigned maximumIncarnations=256,commandLeaseMs=1000,maximumSafetyAgeMs=2000;
};
class GameWorkerSession final:public WorkerFleet {
   public:
    GameWorkerSession(std::filesystem::path identityHelper,std::filesystem::path detectorHelper,GameProcessTarget target,
        GameDetectionPolicy detection,GameGuardPolicy game,GameProfitBinding binding,GameWorkerPolicy policy,const SessionIntent &,SessionToken);
    ~GameWorkerSession()override;
    GameWorkerSession(const GameWorkerSession &)=delete;
    GameWorkerSession &operator=(const GameWorkerSession &)=delete;
    bool start(unsigned slots,GameGuardTime)override;
    bool launch(unsigned,std::filesystem::path,std::vector<std::string>,unsigned drainMs,GameGuardTime)override;
    void tick(GameGuardTime,const WorkerFleetEvidence &)override;
    WorkerFleetState state()const override;
    WorkerFleetSnapshot snapshot(unsigned)const override;
    std::vector<std::string> takeLines(unsigned)override;
    const GameGuardSnapshot &gameSnapshot()const; // private in-process evidence; never exported to public stats
    void requestStop()override;
    void stop()override;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
