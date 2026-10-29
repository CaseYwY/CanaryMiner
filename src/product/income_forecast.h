// Pure, bounded forecast composition. No fetcher, storage mutation, controller or real reward qualification.
#pragma once
#include "credit_process.h"
#include "electricity.h"
#include "financial_view.h"
#include <memory>

namespace om {
enum class ForecastStatus { Ok, Partial, Unavailable, Unsupported, Invalid, Attribution, Coverage, Stale, Clock, Capacity };
const char *forecastStatusName(ForecastStatus status);
enum class ForecastQualification { Unsupported, SyntheticFixture };
struct ForecastRatio {
    ExactDecimal numerator,denominator;
    bool format(unsigned places,std::string *out)const{return numerator.formatRatio(denominator,places,out);}
};
struct ForecastWindows {
    ForecastStatus status=ForecastStatus::Unavailable;
    CivilClock clock;
    CivilDay today;
    int64_t nowUtcMs=0,cutExclusiveMs=0;
    CreditQuerySpec todayCredits,rolling24h,cumulative;
};
// Posted events at now are included. Future work starts at now+1; the day is exactly [begin,end).
ForecastWindows incomeForecastWindows(const CivilClock &clock,const CreditSourceContract &binding,int64_t nowUtcMs);
struct ForecastCredits {
    CreditSourceContract binding;
    CreditTotals totals;
    // Exact committed source snapshot accompanying a coherent query bundle, never a queued/Prepared target.
    uint64_t sequence=0;
    std::string snapshotId;
};
enum class ForecastWorkState { UserMining, Donation, Paused, Switching };
struct ForecastWorkInterval {
    int64_t beginUtcMs=0,endUtcMs=0;
    ForecastWorkState state=ForecastWorkState::Paused;
    ExactDecimal userTmacsPerSecond; // non-user slots must explicitly be zero, not counted and then deducted again
};
struct ForecastWork {
    ForecastQualification qualification=ForecastQualification::Unsupported;
    CreditSourceContract binding;
    std::string profileKey; // exact local measured-work model identity; no ordinal/share-count calibration
    std::vector<ForecastWorkInterval> intervals;
};
struct RewardPeriod {
    int64_t beginUtcMs=0,endUtcMs=0;
    ForecastCredits credits;
    ForecastWork work;
};
struct RewardCalibrationInput {
    ForecastQualification qualification=ForecastQualification::Unsupported;
    CreditSourceContract binding;
    std::string modelKey,profileKey,earningAlignmentContract; // fixture.* only; posted time alone cannot prove earning alignment
    int64_t calibratedUtcMs=0,validUntilUtcMs=0;
    std::vector<RewardPeriod> periods;
};
struct RewardCalibration {
    ForecastStatus status=ForecastStatus::Unavailable;
    ExactDecimal creditedTokens,userTmacMilliseconds;
    uint64_t coveredMs=0;
    size_t periods=0;
};
// Retains validated immutable input so forecast evaluation cannot silently accept caller-forged calibration sums.
class RewardModel {
   public:
    const RewardCalibration &calibration()const{return calibration_;}
    const RewardCalibrationInput &input()const{return input_;}
   private:
    RewardCalibrationInput input_;
    RewardCalibration calibration_;
    RewardModel(RewardCalibrationInput input,RewardCalibration calibration):input_(std::move(input)),calibration_(std::move(calibration)){}
    friend std::shared_ptr<const RewardModel> calibrateReward(const RewardCalibrationInput &,ForecastStatus *);
};
std::shared_ptr<const RewardModel> calibrateReward(const RewardCalibrationInput &input,ForecastStatus *status=nullptr);
struct ForecastSchedule {
    ForecastWork work;
    std::string assumptionKey; // fixture.*; explicit full remaining day including pauses/donation/switching
    int64_t declaredUtcMs=0;
    uint64_t maxAgeMs=120000;
};
struct ForecastCurrencyFx {
    ForecastQualification qualification=ForecastQualification::Unsupported;
    std::string baseCurrency,source;
    ExactDecimal usdPerUnit;
    int64_t observedUtcMs=0;
    uint64_t maxAgeMs=120000;
};
struct ForecastElectricity {
    bool configured=false; // controls net-row visibility; a configured signed/zero price is meaningful
    CreditSourceContract attributedProduction;
    TariffPlan plan;
    BillOpening opening;
    PowerTrace fullDayPower; // explicit estimated full-day scenario, not accrued actual monetary state
    std::optional<ForecastCurrencyFx> fx;
};
struct IncomeForecastInput {
    CreditSourceContract binding;
    CivilClock clock;
    int64_t nowUtcMs=0;
    std::optional<int64_t> valuationUtcMs; // default report instant; later same-day quotes do not refresh credit/forecast facts
    uint64_t nowMonoMs=0;
    ForecastCredits today,rolling24h,cumulative;
    std::shared_ptr<const RewardModel> reward;
    ForecastSchedule remaining;
    std::optional<MarketQuote> tokenQuote,fxQuote;
    MarketFreshnessPolicy marketPolicy;
    ForecastElectricity electricity;
};
struct ForecastProduction {
    ForecastStatus status=ForecastStatus::Unavailable;
    CreditTotals facts;
    std::optional<ExactDecimal> knownTokens;
    bool complete=false;
    std::optional<ForecastRatio> usdt,usd;
};
struct IncomeForecast {
    ForecastStatus status=ForecastStatus::Unavailable,remainingStatus=ForecastStatus::Unavailable,todayStatus=ForecastStatus::Unavailable,netStatus=ForecastStatus::Unavailable;
    ForecastWindows windows;
    int64_t valuationUtcMs=0;
    CreditScope scope=CreditScope::Worker;
    ForecastProduction todayActual,actual24h,actualCumulative;
    MarketLatest latest;
    std::optional<ForecastRatio> expectedRemaining,todayTokens,todayUsdt,todayUsd,netTodayUsd;
    uint64_t userMs=0,donationMs=0,pausedMs=0,switchMs=0;
    bool electricityConfigured=false;
    PowerBasis electricityBasis=PowerBasis::Unknown;
    BillEstimate electricity;
    std::optional<ElectricityAmount> electricityUsd;
};
IncomeForecast incomeForecast(const IncomeForecastInput &input);
// Exact display projection; private scope/model/plan identifiers are not emitted.
std::vector<std::string> incomeForecastLines(const IncomeForecast &view);
FinancialView incomeForecastDashboardView(const IncomeForecast &view);
} // namespace om
