// Default-off experiment command authority. No sampler, native transport, launch, persistence or source qualification.
#pragma once
#include "work_measurement.h"
namespace om {
class WorkExperimentBudget;
struct WorkExperimentPolicy {
    bool enabled=false;
    uint64_t commandLeaseMs=1000,renewalMarginMs=100,handoffTimeoutMs=5000;
};
bool validWorkExperimentPolicy(const WorkExperimentPolicy &);
enum class WorkExperimentState { Dormant, Disabled, Running, Holding, Transferred, Stopped, Faulted };
struct WorkExperimentRunBinding {
    WorkerCommand command;
    WorkMeasurementTime selected;
    std::shared_ptr<const WorkProfilePlan> plan;
    WorkProfileSample source;
    uint64_t sequence=0,requestRevision=0,sourceValidUntilMonoMs=0,measurementDeadlineMonoMs=0,safetyGameDeadlineMonoMs=0;
};
struct WorkExperimentRecord {
    std::shared_ptr<const WorkMeasurementRecord> measurement;
    std::vector<std::shared_ptr<const WorkExperimentRunBinding>> runs;
};
struct WorkExperimentSnapshot {
    WorkExperimentState state=WorkExperimentState::Dormant;
    WorkProfileStatus status=WorkProfileStatus::Invalid;
    bool pending=false,revokeRequired=false;
    uint64_t commandRevision=0,handoffUntilMonoMs=0;
    std::shared_ptr<const WorkExperimentRunBinding> lastRun;
    std::optional<WorkerReceipt> receipt; // exact latest settlement, including a late historical original after stop
    std::shared_ptr<const WorkExperimentRecord> record; // complete evidence is separate from control ownership
};
// Owns capture and exclusively calls issue/ack after actual initial Pause plus baseline. Every method and borrowed
// object belongs to one control thread. Poll current full safety/game permission, submit each returned command once,
// and revoke the native channel immediately on revokeRequired. Retest admission/reaping remains external.
class WorkExperimentOwner {
   public:
    WorkExperimentOwner(WorkExperimentPolicy,WorkProfilePlan,WorkerControlBinding,WorkerControlIssuer &,const SessionIntent &,SessionToken,WorkExperimentBudget *budget=nullptr);
    ~WorkExperimentOwner();
    WorkExperimentOwner(const WorkExperimentOwner &)=delete;
    WorkExperimentOwner &operator=(const WorkExperimentOwner &)=delete;
    bool start(WorkProfileSample baseline,WorkMeasurementTime,const WorkProfileConditions &,uint64_t permissionUntilMonoMs);
    bool append(uint64_t sequence,WorkProfileSample,WorkMeasurementTime,const WorkProfileConditions &,uint64_t permissionUntilMonoMs);
    std::optional<WorkerCommand> poll(WorkMeasurementTime,const WorkProfileConditions &,uint64_t permissionUntilMonoMs);
    WorkerReceiptResult acknowledge(const WorkerReceipt &,WorkMeasurementTime,const WorkProfileConditions &,uint64_t permissionUntilMonoMs);
    void transportFailed();
    void requestStop();
    const WorkExperimentSnapshot &snapshot()const;
    const WorkMeasurementSnapshot &measurement()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
    // Consumed only by the matching economic owner, never by a caller-provided record or arbitrary Pause.
    std::shared_ptr<const WorkExperimentRecord> release(WorkerControlIssuer &,const SessionIntent &,SessionToken,
        WorkMeasurementTime,const WorkProfilePlan &,WorkerReceipt *);
    void failedHandoff(); // successor admission failure only; public late callbacks remain inert after transfer
    friend class EconomicCommandOwner;
};
} // namespace om
