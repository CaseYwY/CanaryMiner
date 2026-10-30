// Historical raw-source alignment, not a WorkProfileSample, live permit, sensor qualification or sampler.
#pragma once
#include "completed_work_capture.h"
#include "work_profile.h"
namespace om {
struct WorkPowerSupport {CompletedWorkTime begin,end;};
struct WorkPowerReading {
    std::string workSourceKey;
    uint64_t generation=0,counterEpoch=0,sequence=0;
    PowerEvidence evidence;
    CompletedWorkTime observed,readBegin,readEnd;
    // Entire source measurement support (including a sensor's averaging/cache window), explicitly supplied.
    // A successful read alone cannot invent it. Missing support remains unavailable for work alignment.
    std::optional<WorkPowerSupport> support;
    std::optional<uint32_t> milliwatts;
};
struct WorkPowerAlignmentPolicy {
    bool enabled=false;
    std::string workSourceKey;
    uint64_t generation=0,counterEpoch=0;
    PowerEvidence evidence;
    unsigned maximumBracketMs=2000,maximumReadMs=500,maximumSourceAgeMs=2000;
};
struct AlignedWorkPower {CompletedWorkInterval work;WorkPowerReading power;};
// Copies both originals. The sample instant is power.observed, never delivery time or a restamped lastBatch.
// Source sequencing, source liveness, receipt binding and independent safety/game/drift still belong to the enclosing owner.
std::shared_ptr<const AlignedWorkPower> alignWorkPower(const WorkPowerAlignmentPolicy &,const CompletedWorkInterval &,
    const WorkPowerReading &,CompletedWorkTime now,WorkProfileStatus *status=nullptr);
} // namespace om
