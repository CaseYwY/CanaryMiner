// One fixed game runtime and private parent identity across every admitted worker incarnation.
#pragma once
#include "worker_fleet.h"
#include "game_runtime.h"
#include "economic_command_owner.h"
#include "work_experiment_owner.h"
#include "work_experiment_budget.h"
namespace om {
struct GameWorkerPolicy {
    unsigned maximumIncarnations=256,commandLeaseMs=1000,maximumSafetyAgeMs=2000;
};
struct GameExperimentPolicy {WorkExperimentPolicy commands;WorkExperimentBudgetPolicy budget;};
struct GameEconomicPolicy {
    EconomicCommandPolicy commands;
    EconomicEvaluationPolicy evaluation;
    WorkProfilePlan plan;
    std::optional<GameExperimentPolicy> experiment=std::nullopt; // explicit: measure each incarnation before its economic handoff
};
enum class GameWorkSampleAdmission { Accepted, NotReady, Full, WrongGeneration, WrongBinding, Inactive, Invalid };
struct GameExperimentSnapshot {
    bool configured=false,started=false,samplePending=false;
    uint64_t workerGeneration=0;
    WorkerControlBinding binding;
    std::optional<WorkerReceipt> initialPause;
    WorkExperimentSnapshot commands;
    WorkMeasurementSnapshot measurement;
    WorkExperimentBudgetSnapshot budget;
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
    // Bounded one-sample inbox. Only tick consumes it, after current safety/game and original receipt admission.
    // Sources remain explicitly synthetic; raw work/power alignment alone cannot supply this API's qualification.
    GameWorkSampleAdmission submitExperimentSample(unsigned slot,const WorkerControlBinding &,uint64_t sequence,WorkProfileSample,GameGuardTime);
    GameExperimentSnapshot experimentSnapshot(unsigned slot)const;
    // One explicit operator press over the current known game pause. Control thread only; the fixed
    // game runtime owns release and safety precedence. No preference or default is written.
    bool requestForceResume()override;
    void cancelForceResume()override;
    bool forceResumeActive()const override;
    bool forceResumeAvailable()const override;
    void requestStop()override;
    void stop()override;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
