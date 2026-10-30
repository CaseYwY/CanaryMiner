// Exact reference-only resume/switch margins. Freshness, actual application and temporal hysteresis belong to the owner.
#pragma once
#include "economic_study.h"
namespace om {
struct EconomicChoicePolicy {
    bool enabled=false;
    ExactDecimal resumeMinimumUsdPerSecond,switchMinimumGainUsdPerSecond,switchMinimumFraction;
};
enum class EconomicCurrentKind { Unknown, Paused, AppliedWait };
struct EconomicCurrentChoice {EconomicCurrentKind kind=EconomicCurrentKind::Unknown;unsigned hostWaitMs=0;};
enum class EconomicChoiceState { Disabled, Invalid, Unavailable, Capacity, PauseNonpositive, PauseIneligible, PauseMargin, Hold, Resume, Switch };
struct EconomicChoice {
    EconomicChoiceState state=EconomicChoiceState::Unavailable;
    std::optional<size_t> point;
    std::optional<unsigned> hostWaitMs;
    bool referenceOnly=true,statisticalConfidenceAvailable=false;
};
bool validEconomicChoicePolicy(const EconomicChoicePolicy &);
// The current mode is an explicit caller assertion, not a worker receipt. Unknown never becomes Paused.
// No clock/lease/source qualification or worker command is produced here. Thresholds are declared heuristics.
EconomicChoice economicChoice(const EconomicStudy *,const EconomicChoicePolicy &,EconomicCurrentChoice);
} // namespace om
