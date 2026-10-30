// Explicit-parent capture of original samples against the actual worker issuer. No source I/O or command issuance.
#pragma once
#include "work_profile.h"
#include "worker_control.h"
namespace om {
struct WorkMeasurementTime {int64_t utcMs=0;uint64_t monotonicMs=0;};
enum class WorkMeasurementState { Dormant, AwaitingApplication, Warming, Measuring, Draining, Complete, Stopped, Faulted };
struct WorkMeasurementRecord {
    WorkerControlBinding binding;
    WorkMeasurementTime admitted,completed;
    WorkProfileInput input;
    std::vector<WorkerReceipt> receipts; // contiguous original settlements including initial/final Pause, at most 8192
};
struct WorkMeasurementSnapshot {
    WorkMeasurementState state=WorkMeasurementState::Dormant;
    WorkProfileStatus status=WorkProfileStatus::Invalid;
    bool revokeRequired=false;
    size_t sampleCount=0,completedWindows=0;
    uint64_t sequence=0,sampleDueMonoMs=0,deadlineMonoMs=0,requestRevision=0,sourceValidUntilMonoMs=0;
    std::optional<WorkProfileSample> latestSample; // original, never restamped by poll or receipt consumption
    std::optional<unsigned> requestedHostWaitMs; // a requested experiment step, never permission or an issued command
    ElectricityAmount observedEnergy;
    std::shared_ptr<const WorkMeasurementRecord> record; // private immutable original, not source qualification or a built profile
};
// All calls and the borrowed issuer/intent belong to one control thread. The enclosing owner must exclusively
// control the experiment and close its native channel on revokeRequired. It must separately qualify every source.
class WorkMeasurementOwner {
   public:
    WorkMeasurementOwner(WorkProfilePlan,WorkerControlBinding,WorkerControlIssuer &,const SessionIntent &,SessionToken);
    ~WorkMeasurementOwner();
    WorkMeasurementOwner(const WorkMeasurementOwner &)=delete;
    WorkMeasurementOwner &operator=(const WorkMeasurementOwner &)=delete;
    bool start(WorkProfileSample baseline,WorkMeasurementTime,const WorkProfileConditions &); // actual initial Paused; baseline is sequence 1
    const WorkMeasurementSnapshot &poll(WorkMeasurementTime,const WorkProfileConditions &);
    bool append(uint64_t sequence,WorkProfileSample,WorkMeasurementTime,const WorkProfileConditions &);
    void requestStop();
    const WorkMeasurementSnapshot &snapshot()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
