// One fixed game runtime and private parent identity across every admitted worker incarnation.
#pragma once
#include "worker_fleet.h"
#include "game_runtime.h"
#include "economic_command_owner.h"
namespace om {
struct GameWorkerPolicy {
    unsigned maximumIncarnations=256,commandLeaseMs=1000,maximumSafetyAgeMs=2000;
};
struct GameEconomicPolicy {
    EconomicCommandPolicy commands;
    EconomicEvaluationPolicy evaluation;
    WorkProfilePlan plan;
};
struct GameEconomicSnapshot {
    bool configured=false,commandStarted=false;
    uint64_t contextRevision=0;
    WorkProfileConditions conditions;
    EconomicEvaluationSnapshot evaluation;
    EconomicCommandSnapshot commands;
    EconomicParentBudgetSnapshot budget;
};
class GameWorkerSession final:public WorkerFleet {
   public:
    GameWorkerSession(std::filesystem::path identityHelper,std::filesystem::path detectorHelper,GameProcessTarget target,
        GameDetectionPolicy detection,GameGuardPolicy game,GameProfitBinding binding,GameWorkerPolicy policy,const SessionIntent &,SessionToken,
        std::vector<GameEconomicPolicy> economics={}); // empty preserves non-economic control; otherwise one frozen policy per slot
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
    // Control-thread source notification, applied by the next tick before any command decision. The plan is frozen.
    bool setEconomicContext(unsigned slot,uint64_t revision,bool drift=false);
    OptimizationScheduleSnapshot economicSchedule(unsigned slot)const;
    EconomicSubmission submitEconomic(unsigned slot,const OptimizationTicket &,uint64_t revision,EconomicInput,GameGuardTime);
    OptimizationCompletion completeEconomic(unsigned slot,const OptimizationTicket &,OptimizationOutcome,GameGuardTime); // external lanes/unsubmitted evaluation only
    GameEconomicSnapshot economicSnapshot(unsigned slot)const; // private originals; never public stats or logs
    void requestStop()override;
    void stop()override;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
