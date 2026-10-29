// Offline exact candidate comparison. No scheduler, live authority, source fetch, storage or hardware access.
#pragma once
#include "income_forecast.h"
#include "work_profile.h"
namespace om {
enum class EconomicStatus { Ok, Disabled, Unsupported, Invalid, Unavailable, Attribution, Coverage, Clock, Stale, Capacity, Cancelled, Unsafe, Game };
struct EconomicPolicy {
    bool enabled=false;
    uint64_t horizonMs=3600000,maximumMarketAgeMs=120000,maximumFeedSkewMs=30000;
    unsigned lookbackMinutes=480; // economic window, independent of dashboard averages and observation cadence
    ExactDecimal revenueHaircut; // explicit [0,1] heuristic, NOT a confidence level or a second pool fee
};
struct EconomicOverhead {
    bool permitted=false,declared=false,repeatMeasurement=false;
    std::string key;
    int64_t observedUtcMs=0;
    uint64_t validForMs=120000,transitionMs=0;
    ElectricityAmount transitionEnergy; // future energy in the same profile basis; zero duration requires zero energy
};
struct EconomicElectricity {
    bool configured=false;
    TariffPlan plan;
    BillOpening opening; // explicit scenario at future begin; never inferred from an unobserved register
    std::optional<ForecastCurrencyFx> fx;
};
struct EconomicInput {
    EconomicPolicy policy;
    uint64_t generation=0,nowMonoMs=0;
    int64_t nowUtcMs=0;
    WorkProfilePlan currentPlan;
    WorkProfileConditions conditions;
    std::shared_ptr<const WorkProfile> profile;
    CreditSourceContract production;
    std::shared_ptr<const RewardModel> reward;
    std::optional<MarketCandles> pearl,fx;
    EconomicElectricity electricity;
    std::vector<EconomicOverhead> candidates; // one per profile point, same order; unavailable permitted inputs reject the whole comparison
};
struct EconomicCandidate {
    size_t point=0;unsigned hostWaitMs=0;
    uint64_t elapsedMs=0,runMs=0,transitionMs=0,testMs=0;
    ForecastRatio expectedTokens,estimatedKwh,revenueUsd,costUsd,netUsd,netUsdPerSecond;
};
struct EconomicSummary {
    int64_t beginUtcMs=0,endUtcMs=0;
    std::string profileRecordKey;
    MarketWindow prices;
    std::vector<EconomicCandidate> candidates; // permitted points only; exact signed scores, including nonpositive candidates
    std::optional<size_t> bestPoint; // absent means no strictly positive measured eligible candidate
    bool gpuBoardProxy=true,statisticalConfidenceAvailable=false;
};
// Calibration must explicitly name this complete measured record, not just a reusable plan or device model.
std::string economicProfileKey(const WorkProfile &profile);
class EconomicStudy {
   public:
    const EconomicInput &input()const{return input_;}
    const EconomicSummary &summary()const{return summary_;}
   private:
    EconomicInput input_;EconomicSummary summary_;
    EconomicStudy(EconomicInput input,EconomicSummary summary):input_(std::move(input)),summary_(std::move(summary)){}
    friend std::shared_ptr<const EconomicStudy> buildEconomicStudy(const EconomicInput &,EconomicStatus *,const std::atomic<bool> *);
};
std::shared_ptr<const EconomicStudy> buildEconomicStudy(const EconomicInput &input,EconomicStatus *status=nullptr,const std::atomic<bool> *cancel=nullptr);
} // namespace om
