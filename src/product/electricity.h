// Pure local arithmetic, not a supplier adapter, utility bill certification or mining controller.
#pragma once
#include "civil_time.h"
#include "exact_decimal.h"
#include <optional>
#include <vector>

namespace om {
constexpr uint32_t wattMsPerKwh = 3600000000U;
enum class ElectricityStatus { Ok, Partial, Unavailable, Invalid, Unsupported, MissingPrice, Conflict, NotYetKnown, ClockFault, Capacity };
const char *electricityStatusName(ElectricityStatus status);
// Exact amount = numerator / 3,600,000,000. Used for kWh and currency without intermediate rounding.
struct ElectricityAmount {
    ExactDecimal numerator;
    bool format(unsigned places, std::string *out) const { return numerator.formatRatio(wattMsPerKwh,places,out); }
};
std::optional<ElectricityAmount> electricityKwh(const ExactDecimal &kwh);
enum class PowerBasis { Unknown, GpuBoard, WholeSystemWall, DirectIncrementalWall, WholeMeterImport };
enum class PowerOrigin { Unknown, SyntheticFixture, ObservedTelemetry, DeclaredForecast };
struct PowerEvidence {
    PowerBasis basis = PowerBasis::Unknown;
    PowerOrigin origin = PowerOrigin::Unknown;
    std::string scope, adapter;
};
struct PowerInterval { int64_t beginUtcMs = 0, endUtcMs = 0; ExactDecimal watts; };
struct PowerTrace { PowerEvidence evidence; std::vector<PowerInterval> intervals; };
struct PowerObservation {
    int64_t utcMs = 0, monotonicMs = 0;
    std::optional<ExactDecimal> watts;  // absent sensor value is a gap, never zero
};
struct EnergyEstimate {
    ElectricityStatus status = ElectricityStatus::Unavailable;
    ElectricityAmount knownKwh;
    std::optional<ElectricityAmount> completeKwh;
    int64_t beginUtcMs = 0, endUtcMs = 0, coveredMs = 0;
    bool clockDiscontinuity = false;
    PowerTrace coveredTrace;  // explicit bounded sample-and-hold model; no extrapolation after last observation
};
EnergyEstimate sampleHoldEnergy(const PowerEvidence &evidence, const std::vector<PowerObservation> &samples,
                               int64_t beginUtcMs, int64_t endUtcMs, int64_t maximumGapMs);

enum class TierMode { None, Progressive, AllUnits };
struct TariffBand {
    std::optional<ExactDecimal> throughKwh;  // inclusive selection for all-units; last band must be unbounded
    ExactDecimal ratePerKwh;                // signed retail currency/kWh, not asset price
};
struct CalendarRate {
    std::string key;
    unsigned beginMinute = 0, endMinute = 1440, weekdayMask = 127, monthMask = 4095, priority = 0;
    std::optional<CivilDate> date;
    int64_t publishedUtcMs = 0;
    ExactDecimal ratePerKwh;
};
struct DynamicRate {
    int64_t beginUtcMs = 0, endUtcMs = 0, publishedUtcMs = 0;
    ExactDecimal ratePerKwh;
};
enum class TimeRateMode { Constant, Calendar, Dynamic };
struct TariffRevision {
    std::string key;
    int64_t beginUtcMs = 0, endUtcMs = 0, publishedUtcMs = 0;
    TimeRateMode timeMode = TimeRateMode::Constant;
    ExactDecimal baseRatePerKwh;              // explicit calendar fallback; dynamic must leave this zero
    std::vector<CalendarRate> calendar;
    std::vector<DynamicRate> dynamic;
    std::vector<TariffBand> bands;             // additive shared whole-import register, not per-TOU tier resets
    ExactDecimal chargePerCivilDay;
};
enum class TariffOrigin { Unknown, SyntheticFixture, UserDeclaredOffline };
enum class TariffRounding { Unsupported, FinalDisplayOnly };
struct TariffPlan {
    bool declared = false;
    TariffOrigin origin = TariffOrigin::Unknown;
    // Each identity axis is explicit; no locale, geolocation, currency-symbol or system-clock inference.
    std::string key, territory, supplier, product, currency;
    CivilClock clock;
    TierMode tiers = TierMode::None;
    TariffRounding rounding = TariffRounding::Unsupported;
    std::vector<TariffRevision> revisions;     // immutable chronological rates; changes do not reset the cycle
    ExactDecimal cycleCharge;
    std::optional<ExactDecimal> minimumCycleSubtotal;
    std::optional<ExactDecimal> subtotalMultiplier;  // explicit single multiplier on entire subtotal, no inferred tax order
    uint32_t unsupportedRules = 0;             // any demand/export/prepaid/other unmodelled rule rejects evaluation
};
struct BillOpening {
    bool declared = false;
    std::string planKey, timeDatabaseVersion;  // after cycle start, bind the data version used by the existing state
    PowerEvidence evidence;  // an opening quantity cannot be reused for a different meter/basis/source
    int64_t cycleBeginUtcMs = 0, cycleEndUtcMs = 0, throughUtcMs = 0;
    ElectricityAmount importedKwh, accruedSubtotal;
};
struct BillEstimate {
    ElectricityStatus status = ElectricityStatus::Unavailable;
    std::optional<ElectricityAmount> charge;
    std::optional<BillOpening> ending;
    std::string currency, planKey, territory, supplier, product, timeDatabaseVersion;
    TariffOrigin origin = TariffOrigin::Unknown;
    CivilClock clock;
    PowerEvidence evidence;
    int64_t beginUtcMs = 0, endUtcMs = 0;
    int64_t decisionUtcMs = 0;
    bool cycleSettled = false;
};
// Full coverage only. Horizon stays within one explicitly supplied cycle (<=370 elapsed days).
// All-units/floor require the remainder through cycle end; no short-horizon free allowance or assumed future load.
BillEstimate evaluateBill(const TariffPlan &plan, const BillOpening &opening, const PowerTrace &trace,
                         int64_t endUtcMs, int64_t decisionUtcMs);
struct CounterfactualBill {
    ElectricityStatus status = ElectricityStatus::Unavailable;
    BillEstimate withoutMining, withMining;
    std::optional<ElectricityAmount> incrementalCharge;
};
// Both complete WholeMeterImport trajectories must share scope, adapter and origin; caller supplies the whole remainder.
CounterfactualBill counterfactualBill(const TariffPlan &plan, const BillOpening &opening,
                                    const PowerTrace &withoutMining, const PowerTrace &withMining,
                                    int64_t endUtcMs, int64_t decisionUtcMs);
}  // namespace om
