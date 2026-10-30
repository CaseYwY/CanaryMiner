// One immutable offline study's session-bound validity. A usable result is not mining or switching authority.
#pragma once
#include "economic_study.h"
#include "optimization_schedule.h"
namespace om {
struct EconomicUsePolicy {bool enabled=false;uint64_t maximumResultAgeMs=120000;};
enum class EconomicUseState { Dormant, Disabled, Invalid, Attribution, Eligible, NoPositiveCandidate, Expired, Unsafe, Game, Drift, ClockFault, Stopped };
struct EconomicUseSnapshot {
    EconomicUseState state=EconomicUseState::Dormant;
    uint64_t epoch=0,validUntilMonoMs=0;
    OptimizationTime evaluated;
    std::optional<size_t> point;
    std::optional<unsigned> hostWaitMs;
};
// Keep one gate for the entire lifetime of a result. Failure/stop cannot be cleared with later good conditions.
// The immutable study preserves all original source/configuration identities; a refreshed study needs a new gate.
class EconomicStudyUse {
   public:
    EconomicStudyUse(std::shared_ptr<const EconomicStudy>,EconomicUsePolicy,const SessionIntent &,SessionToken);
    EconomicStudyUse(const EconomicStudyUse &)=delete;
    EconomicStudyUse &operator=(const EconomicStudyUse &)=delete;
    const EconomicUseSnapshot &check(const WorkProfilePlan &currentPlan,OptimizationTime,const WorkProfileConditions &);
    void requestStop();
    const EconomicUseSnapshot &snapshot()const{return view_;} // no clock refresh or permission from reading a snapshot
   private:
    std::shared_ptr<const EconomicStudy> study_;EconomicUsePolicy policy_;const SessionIntent &intent_;SessionToken token_;
    EconomicUseSnapshot view_;bool bound_=false,terminal_=false,ceilingBound_=false;
    int64_t wallLimit_=253402300799999LL;uint64_t monoLimit_=UINT64_MAX,ceiling_=0;
    OptimizationTime anchor_,last_;std::string planKey_;
    void halt(EconomicUseState);
    bool bind();
    bool wallBound(int64_t observed,uint64_t age);
    bool monoBound(uint64_t observed,uint64_t age);
};
} // namespace om
