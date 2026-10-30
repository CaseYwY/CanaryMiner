// Parent-slot reservation accounting. No launch/reaping proof, source qualification or physical energy cutoff.
#pragma once
#include "work_measurement.h"
namespace om {
struct WorkExperimentBudgetPolicy {
    bool enabled=false;
    unsigned maximumAttempts=1;
    uint64_t minimumRetestMs=3600000,maximumReservedElapsedMs=86400000;
    ElectricityAmount maximumReservedEnergy; // positive explicit ceiling; zero is not an unlimited budget
};
enum class WorkExperimentBudgetState { Dormant, Disabled, Running, Stopped, Faulted };
struct WorkExperimentReservation {
    WorkerReceipt initialPause;
    WorkMeasurementTime admitted;
    uint64_t number=0,elapsedMs=0;
    ElectricityAmount energy;
};
struct WorkExperimentBudgetSnapshot {
    WorkExperimentBudgetState state=WorkExperimentBudgetState::Dormant;
    WorkProfileStatus status=WorkProfileStatus::Invalid;
    unsigned attempts=0;
    bool attached=false,completed=false,transferred=false;
    uint64_t reservedElapsedMs=0,observedElapsedMs=0;
    ElectricityAmount reservedEnergy,observedEnergy; // complete-record observations only; reservation is never refunded
    std::shared_ptr<const WorkExperimentReservation> latest;
};
// Outlives all experiment owners of one logical slot, sharing their exact explicit parent. Admission reserves the
// whole plan before Run, and retirement never refunds it. Only a completed/transferred attempt with the same
// stopped/settled issuer can retire. The enclosing native owner MUST additionally prove eligible reaping/no overlap.
class WorkExperimentBudget {
   public:
    WorkExperimentBudget(WorkExperimentBudgetPolicy,WorkProfilePlan,const SessionIntent &,SessionToken);
    ~WorkExperimentBudget();
    WorkExperimentBudget(const WorkExperimentBudget &)=delete;
    WorkExperimentBudget &operator=(const WorkExperimentBudget &)=delete;
    bool start(WorkMeasurementTime);
    bool retire(WorkerControlIssuer &,WorkMeasurementTime);
    void requestStop();
    const WorkExperimentBudgetSnapshot &snapshot()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
    WorkProfileStatus admit(const void *,WorkerControlIssuer &,const SessionIntent &,SessionToken,const WorkProfilePlan &,WorkMeasurementTime);
    WorkProfileStatus check(const void *,WorkMeasurementTime);
    bool complete(const void *,const WorkMeasurementRecord &,const ElectricityAmount &,WorkMeasurementTime);
    bool transfer(const void *,const WorkerReceipt &,WorkMeasurementTime);
    void fail(const void *,WorkProfileStatus);
    friend class WorkExperimentOwner;
};
} // namespace om
