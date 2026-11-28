// Pure local arithmetic, not a supplier adapter, utility bill certification or mining controller.
#pragma once
#include "civil_time.h"
#include "exact_decimal.h"
#include <array>
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace om {
constexpr uint32_t wattMsPerKwh = 3600000000U;
enum class ElectricityStatus { Ok, Partial, Unavailable, Invalid, Unsupported, MissingPrice, Conflict, NotYetKnown, ClockFault, Capacity, MeterBoundary, MeterRoundingBoundary, SeasonalDayBoundary, DemandWindowBoundary, DemandHistoryNotYetKnown, DemandApparentEvidence, ExportEvidenceMissing, ExportRateBoundary, TaxPeriodBoundary, TaxScheduleBoundary, TaxMeterBoundary };
const char *electricityStatusName(ElectricityStatus status);
// Exact amount = numerator / 3,600,000,000. Used for quantities and currency without intermediate rounding.
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
struct PowerInterval {
    int64_t beginUtcMs = 0, endUtcMs = 0;
    ExactDecimal watts;
    std::optional<ExactDecimal> apparentVa{};  // independent whole-meter average
    std::optional<ExactDecimal> exportWatts{};  // positive in the direction from meter to grid
};
struct PowerTrace { PowerEvidence evidence; std::vector<PowerInterval> intervals; };
// A measured quantity over the full interval carries no implied power profile within it.
struct MeterImportInterval {
    int64_t beginUtcMs = 0, endUtcMs = 0;
    ElectricityAmount importedKwh;
    std::optional<ElectricityAmount> apparentKvah{};  // delta from the paired apparent-energy register
    std::optional<ElectricityAmount> exportedKwh{};  // delta from the separate export register
};
struct MeterImportTrace { PowerEvidence evidence; std::vector<MeterImportInterval> intervals; };
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
// The caller supplies a validated progressive band schedule and scaled quantities.
bool progressiveTierChange(const std::vector<TariffBand> &bands,const ExactDecimal &before,
                           const ExactDecimal &after,ExactDecimal &change);
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
enum class TariffRounding { Unsupported, FinalDisplayOnly, MeterHalfHourKwhHalfEven2 };
struct DemandRatchet {
    unsigned lookbackMonths = 0;
    ExactDecimal fraction;
};
inline constexpr size_t maxDemandPeriods = 16, maxDemandCharges = 8;
struct DemandPeriod {
    unsigned beginMinute = 0, endMinute = 1440, weekdayMask = 127, monthMask = 4095;
    std::optional<CivilDate> date{};
    bool eligible = true;
};
struct DemandChargeRule {
    unsigned windowMinutes = 0;
    unsigned offsetMinutes = 0;  // UTC phase from the Unix epoch, less than one window
    int64_t publishedUtcMs = 0;
    ExactDecimal ratePerUnit;
    std::optional<DemandRatchet> ratchet{};
    bool apparentPower = false;
    std::optional<unsigned> powerFactorVaPlaces{};
    std::vector<DemandPeriod> eligiblePeriods{};  // empty uses all windows in the cycle
    bool apparent() const { return apparentPower; }
    const char *unit() const { return apparent() ? "kVA" : "kW"; }
    bool charge(const ElectricityAmount &peak, ElectricityAmount *out) const {
        return peak.numerator.multiply(ratePerUnit, &out->numerator);
    }
};
struct NamedDemandCharge {
    std::string key;
    DemandChargeRule rule;
};
struct NamedDemandOpening {
    std::string key;
    ElectricityAmount peak;
    std::vector<ElectricityAmount> history{};
};
struct ExportPaymentRule {
    int64_t publishedUtcMs = 0;
    ExactDecimal ratePerKwh;
    std::vector<DynamicRate> dynamic{};  // when present, replaces the flat rate
};
enum class TaxComponent { Energy, DailyFee, CycleFee, Demand, MinimumAdjustment, Count };
inline constexpr unsigned taxComponentCount = static_cast<unsigned>(TaxComponent::Count);
inline constexpr unsigned taxComponentBit(TaxComponent component) { return 1u << static_cast<unsigned>(component); }
inline constexpr std::array<const char *, taxComponentCount> taxComponentNames{
    "energy", "daily_fee", "cycle_fee", "demand", "minimum_adjustment"
};
inline constexpr size_t maxTaxPeriods = 16, maxTaxRules = 16;
enum class TaxKind { ImportedKwh, BilledKwh, Fraction };
struct OrderedTaxRule {
    std::string key;
    TaxKind kind = TaxKind::Fraction;
    ExactDecimal rate;
    unsigned taxable = 0, included = 0;
    std::vector<size_t> priorTaxes;
    bool addsCharge() const { return kind == TaxKind::Fraction ? taxable || !priorTaxes.empty() : !included; }
};
struct TariffTaxPeriod {
    int64_t beginUtcMs = 0, endUtcMs = 0, publishedUtcMs = 0;
    std::vector<OrderedTaxRule> rules;
};
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
    uint32_t unsupportedRules = 0;             // any unmodelled rule rejects evaluation
    std::optional<DemandChargeRule> demandCharge;
    std::optional<ExportPaymentRule> exportPayment{};  // independent payout, outside the supply subtotal
    bool restrictedCreditAtClose = false;
    std::vector<TariffTaxPeriod> taxPeriods{};
    bool taxByServiceTime = false;
    std::vector<NamedDemandCharge> demandCharges{};
    bool hasDemand() const { return demandCharge || !demandCharges.empty(); }
    const DemandChargeRule *apparentDemandRule() const;
    bool hasDemandHistory() const;
};
struct BillTaxOpening {
    std::array<ElectricityAmount, taxComponentCount - 1> components;
    ElectricityAmount billedKwh;  // independent from measured import only for rounded tariffs
};
struct BillTaxSegment {
    int64_t beginUtcMs = 0;
    ElectricityAmount importedKwh;
    BillTaxOpening bases;
};
struct BillTaxLine {
    std::string key;
    TaxKind kind = TaxKind::Fraction;
    unsigned included = 0;
    ElectricityAmount base, added;
    std::optional<int64_t> periodBeginUtcMs{};
};
struct BillTaxTotal {
    std::vector<BillTaxLine> lines;
    ElectricityAmount minimumAdjustment, added, total;
};
struct BillExportOpening {
    ElectricityAmount exportedKwh, accruedPayment;
};
inline constexpr size_t maxRestrictedCreditLots = 16;
struct RestrictedCreditLot {
    int64_t expiresUtcMs = 0;
    ElectricityAmount remaining;
};
struct BillRestrictedCreditOpening {
    ElectricityAmount accruedImportSubtotal;  // signed energy charges before the subtotal multiplier
    std::vector<RestrictedCreditLot> lots;     // strictly increasing absolute expiry instants
};
struct BillRestrictedCreditAccrual {
    ElectricityAmount eligible, applied, expired, remaining;
    std::optional<ElectricityAmount> cashDue;
};
struct BillOpening {
    bool declared = false;
    std::string planKey, timeDatabaseVersion;  // after cycle start, bind the data version used by the existing state
    PowerEvidence evidence;  // an opening quantity cannot be reused for a different meter/basis/source
    int64_t cycleBeginUtcMs = 0, cycleEndUtcMs = 0, throughUtcMs = 0;
    ElectricityAmount importedKwh, accruedSubtotal;  // pre-multiplier subtotal includes the established demand charge
    std::optional<ElectricityAmount> peakDemand;
    std::vector<ElectricityAmount> demandHistory;  // completed monthly measured peaks, oldest first
    bool apparentDemand = false;  // binds the opening peak and history to the demand rule's unit
    std::optional<BillExportOpening> exports{};  // earned through the opening, before the current trace
    std::optional<BillRestrictedCreditOpening> restrictedCredit{};
    std::optional<BillTaxOpening> tax{};
    std::optional<std::vector<BillTaxSegment>> taxSegments{};
    std::vector<NamedDemandOpening> demandOpenings{};
};
// Apply the declared half-even input conversion to apparent VA. Active energy keeps its original watts.
bool apparentDemandFromPowerFactor(const DemandChargeRule &rule,const ExactDecimal &watts,
                                   const ExactDecimal &factor,ExactDecimal &voltAmps);
ElectricityStatus validateHistoricalDemandPeak(const CivilClock &clock,const DemandChargeRule &rule,
                                               int64_t begin,int64_t end,bool positivePeak);
ElectricityStatus validateDemandHistoryMonth(const TariffPlan &plan,const BillOpening &opening,
                                             size_t monthsBack,int64_t begin,int64_t end);
bool billableDemand(const DemandChargeRule &rule,const BillOpening &opening,
                    ElectricityAmount &peak,ElectricityAmount &ratchetFloor);
bool billableDemand(const DemandChargeRule &rule, const ElectricityAmount &measured,
                    const std::vector<ElectricityAmount> &history,
                    ElectricityAmount &peak, ElectricityAmount &ratchetFloor);
bool demandSubtotal(const TariffPlan &plan, const BillOpening &opening, ElectricityAmount &amount);
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
    std::optional<ElectricityAmount> billedKwh;  // current trace only, when interval rounding is selected
    std::optional<ElectricityAmount> exportPayment{};  // payout from this trace only
    std::optional<BillRestrictedCreditAccrual> restrictedCredit{};
    std::optional<BillTaxTotal> tax{};
    bool netChange(ElectricityAmount &out) const {
        if (!charge) return false;
        if (exportPayment) return charge->numerator.subtract(exportPayment->numerator,&out.numerator);
        if (restrictedCredit) return charge->numerator.subtract(restrictedCredit->applied.numerator,&out.numerator);
        out=*charge;
        return true;
    }
};
// The opt-in rule splits the fee selected at civil midnight by each cycle's share of that day.
enum class DailyFeeAllocation { WholeDay, ProrateByCycleMinutes };
// Structural/opening/time-data validation only; does not promise dynamic-price or trace coverage.
ElectricityStatus validateTariffOpening(const TariffPlan &plan, const BillOpening &opening,
                                        DailyFeeAllocation allocation=DailyFeeAllocation::WholeDay);
// Full coverage only. Horizon stays within one explicitly supplied cycle (<=370 elapsed days).
// All-units/floor require the remainder through cycle end.
BillEstimate evaluateBill(const TariffPlan &plan, const BillOpening &opening, const PowerTrace &trace,
                          int64_t endUtcMs, int64_t decisionUtcMs,
                          DailyFeeAllocation allocation=DailyFeeAllocation::WholeDay,
                          const std::atomic<bool> *cancel=nullptr);
// Exact contiguous-prefix arithmetic. Provisional totals are NOT a payable utility bill or an incremental mining cost.
// All-units reprices the accumulated register; minimum-cycle floors apply only at the explicit cycle end.
struct BillExportAccrual {
    ElectricityAmount newKwh, paymentChange, paymentTotal, netChange, netTotal;
};
struct BillAccrual {
    ElectricityStatus status=ElectricityStatus::Unavailable;
    std::optional<BillOpening> ending;
    std::optional<ElectricityAmount> provisionalChange,provisionalTotal,settledTotal;
    bool settlementDependent=false; // future cycle quantity can change already accrued all-units/floor amounts
    std::optional<ElectricityAmount> billedKwh;  // current trace only; ending.importedKwh retains physical usage
    std::optional<BillExportAccrual> exportAccrual{};
    std::optional<BillRestrictedCreditAccrual> restrictedCredit{};
    std::optional<BillTaxTotal> tax{};
};
BillAccrual accrueBill(const TariffPlan &plan,const BillOpening &opening,const PowerTrace &trace,
                      int64_t endUtcMs,int64_t observedUtcMs,const std::function<bool()> &stop={},
                      DailyFeeAllocation dailyFeeAllocation=DailyFeeAllocation::WholeDay);
// Apply measured whole-meter import once under a single energy rate schedule per interval.
BillAccrual accrueMeterImport(const TariffPlan &plan,const BillOpening &opening,const MeterImportTrace &trace,
                             int64_t endUtcMs,int64_t observedUtcMs,const std::function<bool()> &stop={},
                             DailyFeeAllocation dailyFeeAllocation=DailyFeeAllocation::WholeDay);
// Price a complete export statement period independently of supply accrual and resets.
struct ExportPeriodEstimate {
    ElectricityStatus status = ElectricityStatus::Unavailable;
    ElectricityAmount exportedKwh, payment;
};
ExportPeriodEstimate estimateExportPeriod(const ExportPaymentRule &rule, const PowerTrace &trace,
    int64_t beginUtcMs, int64_t endUtcMs, int64_t observedUtcMs, const std::function<bool()> &stop);
ExportPeriodEstimate estimateExportPeriod(const ExportPaymentRule &rule, const MeterImportTrace &trace,
    int64_t beginUtcMs, int64_t endUtcMs, int64_t observedUtcMs, const std::function<bool()> &stop);
struct CounterfactualBill {
    ElectricityStatus status = ElectricityStatus::Unavailable;
    BillEstimate withoutMining, withMining;
    std::optional<ElectricityAmount> incrementalCharge;
};
// Both complete WholeMeterImport trajectories must share scope, adapter and origin; caller supplies the whole remainder.
CounterfactualBill counterfactualBill(const TariffPlan &plan, const BillOpening &opening,
                                    const PowerTrace &withoutMining, const PowerTrace &withMining,
                                    int64_t endUtcMs, int64_t decisionUtcMs,
                                    const std::atomic<bool> *cancel = nullptr,
                                    DailyFeeAllocation dailyFeeAllocation=DailyFeeAllocation::WholeDay);
}  // namespace om
