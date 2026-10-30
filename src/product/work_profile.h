// Bounded offline measurement audit. No sampler, hardware control, source discovery, storage or financial authority.
#pragma once
#include "electricity.h"
#include "session_intent.h"
#include <atomic>
#include <memory>
namespace om {
enum class WorkProfileStatus { Ok, Disabled, Unsupported, Invalid, Attribution, Coverage, Clock, Capacity, Budget, Cancelled, Stopped, Unsafe, Game, Drift, Stale };
enum class WorkProfileQualification { Unsupported, SyntheticFixture };
struct WorkProfileIdentity {
    std::string deviceKey,platformKey,softwareVersion,driverVersion,kernelKey,workShapeKey,environmentKey;
    PowerEvidence power;
};
// Collision-safe full identity, not a GPU-model/ordinal lookup. Empty means invalid identity.
std::string workProfileFingerprint(const WorkProfileIdentity &identity);
struct WorkProfilePlan {
    bool enabled=false;
    WorkProfileQualification qualification=WorkProfileQualification::Unsupported;
    std::string key;
    WorkProfileIdentity identity;
    std::vector<unsigned> hostWaitMs; // explicit complete-batch wait, 0..1000; never a percentage or hardware frequency
    unsigned telemetryCadenceMs=1000,maximumSampleGapMs=2000;
    uint64_t warmupMs=0,measurementMs=0,maximumElapsedMs=0,validForMs=0;
    unsigned repetitions=0;
    ElectricityAmount maximumEnergy; // exact positive declared ceiling in this power basis, includes warm-up/transitions
};
WorkProfileStatus validateWorkProfilePlan(const WorkProfilePlan &plan);
std::string workProfilePlanFingerprint(const WorkProfilePlan &plan); // includes identity, measurement settings, budgets and freshness
struct WorkProfileSample {
    uint64_t generation=0,counterEpoch=0,monotonicMs=0;
    int64_t utcMs=0;
    std::string fingerprint;
    unsigned appliedHostWaitMs=0;
    bool completeWorkBoundary=false,safetyTelemetryValid=false,thermalSafe=false,gameStateKnown=false,gameActive=false,fault=false;
    std::optional<uint32_t> milliwatts;
    // Cumulative *completed* TMAC, separated before differencing. Shares are never accepted as work/revenue.
    std::optional<ExactDecimal> userTmacs,donationTmacs;
};
struct WorkProfileWindow {
    size_t candidate=0;unsigned repetition=0;
    size_t beginSample=0,measurementSample=0,endSample=0;
};
struct WorkProfileInput {
    WorkProfilePlan plan;
    uint64_t generation=0,counterEpoch=0;
    std::vector<WorkProfileSample> samples; // complete ordered run, including gaps between repeated windows
    std::vector<WorkProfileWindow> windows; // candidate-major, repetition-minor; boundary indices, never interpolated work
};
bool sameWorkProfileInput(const WorkProfileInput &,const WorkProfileInput &); // complete originals, not just plan/identity equivalence
struct WorkProfileRate {
    ExactDecimal completedTmacs;uint64_t elapsedMs=0;
    bool formatPerSecond(unsigned places,std::string *out)const;
};
struct WorkProfileTrial {
    size_t candidate=0;unsigned repetition=0;
    int64_t beginUtcMs=0,endUtcMs=0;
    uint64_t warmupMs=0,elapsedMs=0;
    ExactDecimal userTmacs,donationTmacs;
    ElectricityAmount energy;
};
struct WorkProfilePoint {
    unsigned hostWaitMs=0,repetitions=0;
    uint64_t measuredMs=0;
    ExactDecimal userTmacs,donationTmacs;
    ElectricityAmount energy;
    WorkProfileRate minimumUserRate,maximumUserRate; // observed repetition range, NOT a statistical confidence interval
    WorkProfileRate meanUserRate()const{return {userTmacs,measuredMs};} // ratio of sums, not unweighted mean of rates
};
struct WorkProfileSummary {
    int64_t beginUtcMs=0,endUtcMs=0;
    uint64_t elapsedMs=0,measuredMs=0,overheadMs=0;
    ElectricityAmount totalEnergy,measuredEnergy,overheadEnergy;
    ExactDecimal totalUserTmacs,totalDonationTmacs,overheadUserTmacs,overheadDonationTmacs;
    size_t sampleCount=0;
    bool completeCoverage=false,statisticalConfidenceAvailable=false;
    std::vector<WorkProfileTrial> trials;
    std::vector<WorkProfilePoint> points;
};
class WorkProfile {
   public:
    const WorkProfileInput &input()const{return input_;}
    const WorkProfileSummary &summary()const{return summary_;}
   private:
    WorkProfileInput input_;WorkProfileSummary summary_;
    WorkProfile(WorkProfileInput input,WorkProfileSummary summary):input_(std::move(input)),summary_(std::move(summary)){}
    friend std::shared_ptr<const WorkProfile> buildWorkProfile(const WorkProfileInput &,WorkProfileStatus *,const std::atomic<bool> *);
};
std::shared_ptr<const WorkProfile> buildWorkProfile(const WorkProfileInput &input,WorkProfileStatus *status=nullptr,const std::atomic<bool> *cancel=nullptr);
struct WorkProfileConditions {
    bool safetyTelemetryValid=false,thermalSafe=false,gameStateKnown=false,gameActive=false,fault=false,drift=false;
};
// Single-owner eligibility latch, bound to an already explicit session. A stopped instance cannot be revived.
class WorkProfileUse {
   public:
    WorkProfileUse(std::shared_ptr<const WorkProfile> profile,const SessionIntent &intent,SessionToken token):profile_(std::move(profile)),intent_(intent),token_(token){}
    WorkProfileStatus check(const WorkProfilePlan &currentPlan,int64_t nowUtcMs,const WorkProfileConditions &conditions);
   private:
    std::shared_ptr<const WorkProfile> profile_;const SessionIntent &intent_;SessionToken token_;
    WorkProfileStatus latched_=WorkProfileStatus::Ok;std::optional<int64_t> lastUtcMs_;
};
} // namespace om
