// Offline original-frame comparison only. No sampler, game/process discovery, actuator or real coexistence permit.
#pragma once
#include "exact_decimal.h"
#include <atomic>
#include <optional>
#include <vector>
namespace om {
enum class GameExperienceQualification { Unsupported, SyntheticFixture };
enum class GameExperienceStatus { Ok, Disabled, Unsupported, Invalid, Attribution, Coverage, Clock, Capacity, Budget, Cancelled, Unsafe, OutsideLimits };
// Every field is private, explicit and immutable for a paired comparison; model/launcher names alone are insufficient.
struct GameExperienceIdentity {
    std::string gameBuild,scene,device,driver,minerBuild,kernel,display,settings,environment,adapter;
};
bool sameGameExperienceIdentity(const GameExperienceIdentity &,const GameExperienceIdentity &);
struct GameExperienceLimits {
    // Explicit absolute floors, and maximum regression from the paired no-mining baseline (basis points).
    uint32_t minimumMeanMilliFps=0,minimumLowMilliFps=0;
    unsigned maximumMeanLossBp=0,maximumLowLossBp=0;
    uint64_t maximumFrameNs=0,maximumP99FrameNs=0,maximumMemoryBytes=0;
    int maximumTemperatureMilliC=0;
};
struct GameExperiencePlan {
    bool enabled=false;
    GameExperienceQualification qualification=GameExperienceQualification::Unsupported;
    GameExperienceIdentity identity;
    unsigned hostWaitMs=0,repetitions=0;
    uint64_t warmupNs=0,minimumWindowNs=0,maximumTestNs=0;
    GameExperienceLimits limits;
};
struct GameExperienceWindow {
    GameExperienceIdentity identity;
    uint64_t session=0,sequence=0,beginNs=0,endNs=0,warmupNs=0;
    bool mining=false,complete=false,safetyValid=false,thermalSafe=false,fault=false;
    unsigned hostWaitMs=0;
    std::vector<uint64_t> framesNs; // complete contiguous measured frame durations; sum must equal end-begin
    std::optional<uint64_t> peakMemoryBytes;
    std::optional<int> peakTemperatureMilliC;
};
struct GameExperiencePair {GameExperienceWindow baseline,candidate;};
struct GameExperienceInput {GameExperiencePlan plan;std::vector<GameExperiencePair> pairs;};
// Exact rates: FPS = numerator * 10^9 / denominatorNs. No binary float or display rounding in admission.
struct GameExperienceRate {uint64_t frames=0,denominatorNs=0;};
struct GameExperienceMetrics {
    GameExperienceRate mean,slowestOnePercentMean;
    uint64_t p99FrameNs=0,maximumFrameNs=0,peakMemoryBytes=0;
    int peakTemperatureMilliC=0;
};
struct GameExperienceTrial {GameExperienceMetrics baseline,candidate;bool withinLimits=false;};
struct GameExperienceComparison {
    std::vector<GameExperienceTrial> trials;
    uint64_t totalElapsedNs=0;
    bool withinDeclaredLimits=false,statisticalConfidenceAvailable=false;
};
// Transactional output. Ok means every paired synthetic trial met every declared limit, NOT a mining permission.
// OutsideLimits retains full comparison output; malformed/missing/cancelled input leaves the previous output untouched.
GameExperienceStatus compareGameExperience(const GameExperienceInput &,GameExperienceComparison *,const std::atomic<bool> *cancel=nullptr);
} // namespace om
