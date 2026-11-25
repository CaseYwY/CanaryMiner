// Reviewed offline composition and caller-declared opening; never a measured/reset ledger or mining permission.
#pragma once
#include "electricity.h"
#include "json_dom.h"
#include <array>
#include <cstddef>
#include <optional>
namespace om {
constexpr size_t tariffDeclarationMaxBytes=65536;
inline constexpr char finalDisplayRounding[] = "final_display_only";
inline constexpr char meterHalfHourRounding[] = "meter_import_half_hour_kwh_half_even_2";
struct TariffResetSchedule {
    unsigned day=0;
    CivilClock clock;
    DailyFeeAllocation dailyFeeAllocation = DailyFeeAllocation::WholeDay;
    unsigned annualMonth = 0;  // Zero repeats monthly. Otherwise reset once per year in this month.
};
struct TariffDeclaration {
    std::string country, sourceReference, openingKey;
    uint64_t confirmedUtc=0;
    TariffPlan plan;
    BillOpening opening;
    std::optional<TariffResetSchedule> resetSchedule;
    DailyFeeAllocation dailyFeeAllocation() const {
        return resetSchedule ? resetSchedule->dailyFeeAllocation : DailyFeeAllocation::WholeDay;
    }
};
// Display-neutral rejection sites, not persisted identifiers, source qualification or permission.
enum class TariffProblemKind { Unknown, MissingFields, SchemaFields, DuplicateField, Label, Integer, Decimal, Choice, Collection, NoOutput, Oversized, SchemaVersion, Acknowledgement, Confirmation, Currency, FixedOffset, Date, OpeningQuantity, ArithmeticCapacity, ModelStatus, FuturePublication, InvalidJson };
struct TariffProblem {
    std::string path,message;
    TariffProblemKind kind=TariffProblemKind::Unknown;
    std::vector<std::string> choices{};
    std::optional<ElectricityStatus> modelStatus{};
};
struct TariffCycleBounds {
    int64_t beginUtcMs=0,endUtcMs=0;
};
constexpr unsigned maxTariffComparisonCycles = 12;
struct TariffCycleComparison {
    ElectricityStatus status = ElectricityStatus::Unavailable;
    unsigned failedCycle = 0;
    bool splitsChargedDay = false;
    ElectricityAmount withoutMining, withMining, difference;
};
// The first cycle uses the saved opening. Subsequent complete cycles start at the declared monthly or annual reset.
std::optional<int64_t> tariffComparisonEnd(const TariffDeclaration &d, unsigned cycles);
TariffCycleComparison compareTariffCycles(const TariffDeclaration &d, const std::array<ExactDecimal, 2> &watts,
    unsigned cycles, int64_t decisionUtcMs, const std::atomic<bool> *cancel = nullptr);
// Start the adjacent declared cycle from an opening whose prior cycle is complete.
ElectricityStatus advanceTariffOpening(const TariffDeclaration &d, BillOpening &settled, int64_t publishedThroughUtcMs,
                                       bool &chargedDaySplit);
// Display-neutral private source records. Row counts are not JSON strings; value tokens retain exact source serialization.
struct TariffDeclarationChange {
    enum class Kind { Value, RowCount };
    Kind kind=Kind::Value;
    std::string path,beforeJson,afterJson;
    std::size_t beforeRows=0,afterRows=0;
};
// Templates intentionally leave unknown rates, quantities and identities EMPTY, not zero.
JsonValue tariffDeclarationTemplate();
JsonValue tariffRowTemplate(const std::string &collection);
std::string tariffJson(const JsonValue &value);
// Transactional output; validates every key/type/rule and binds an explicit fixed clock.
bool decodeTariffDeclaration(const JsonValue &root,TariffDeclaration *out,TariffProblem *problem=nullptr);
// Checks the rule and through-time independently of unfinished rates and opening amounts.
bool deriveTariffRecurringCycle(const JsonValue &policy,const JsonValue &through,TariffCycleBounds &bounds,TariffProblem *problem=nullptr);
// Monthly-only clients retain their original contract.
bool deriveTariffMonthlyCycle(const JsonValue &policy,const JsonValue &through,TariffCycleBounds &bounds,TariffProblem *problem=nullptr);
bool parseTariffDeclaration(const std::string &text,JsonValue *root,TariffProblem *problem=nullptr);
// Path/value lines for explicit local review. Never log these: they contain private declarations.
std::vector<std::string> tariffDeclarationChanges(const JsonValue &before,const JsonValue &after);
// Same traversal/order and raw originals, without UI catalogs or preformatted human row-count descriptions. Never log.
std::vector<TariffDeclarationChange> tariffDeclarationChangeRecords(const JsonValue &before,const JsonValue &after);
} // namespace om
