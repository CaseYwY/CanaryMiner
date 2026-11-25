#include "meter_bill.h"
#include "meter_bill_archive.h"
#include "meter_invoice.h"
#include "octopus_meter_consumption.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <variant>
#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace om {
namespace {

using namespace meter_bill_detail;
constexpr size_t maxOrdinaryMeterIntervals = 8192;
constexpr size_t maxAnnualMeterIntervals = 18432;

using MeterSeries = std::variant<std::vector<PowerInterval>, std::vector<MeterImportInterval>>;
struct MeterReadings {
    MeterSeries series;
    std::optional<ExactDecimal> registerStart;
    int64_t octopusObserved = 0;
    uint64_t version = 0;
};

struct MeterBillPaths { std::filesystem::path tariff, intervals, archive, rates, invoice; };

std::optional<MeterBillPaths> meterBillPaths(const MeterBillOptions &options, int argc, const char *const *argv) {
    const auto archiveIndex = options.archiveArg ? options.archiveArg : options.extendArg;
    for (int index : {archiveIndex, options.tariffArg, options.intervalsArg, options.reviewCutoffArg,
                      options.saveArg, options.ratesArg, options.invoiceArg})
        if (index && (index < 3 || index >= argc)) return std::nullopt;
#if defined(_WIN32)
    (void)argv;
    // The narrow CRT argv may lose path characters and best-fit non-ASCII option spellings.
    struct WideArguments {
        LPWSTR *values = nullptr;
        ~WideArguments() { if (values) LocalFree(values); }
    } args;
    int count = 0;
    args.values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args.values || count != argc || std::wstring_view(args.values[1]) != L"meter-bill")
        return std::nullopt;
    const auto matches = [&](int index, std::wstring_view flag) {
        return !index || std::wstring_view(args.values[index-1]) == flag;
    };
    if (!matches(options.archiveArg, L"--open-review") || !matches(options.extendArg, L"--extend-review") ||
        !matches(options.tariffArg, L"--tariff") || !matches(options.intervalsArg, L"--intervals") ||
        !matches(options.invoiceArg, L"--invoice") || !matches(options.ratesArg, L"--octopus-rates"))
        return std::nullopt;
    if (options.reviewCutoffArg &&
        (!matches(options.reviewCutoffArg, L"--as-of-utc-ms") ||
         std::wstring_view(args.values[options.reviewCutoffArg]) != std::to_wstring(options.reviewCutoffUtcMs)))
        return std::nullopt;
    if (options.saveArg &&
        (!matches(options.saveArg, L"--save-review") ||
         std::wstring_view(args.values[options.saveArg]) != std::wstring(options.archiveName.begin(), options.archiveName.end())))
        return std::nullopt;
    const auto path = [&](int i) { return i ? std::filesystem::path(args.values[i]) : std::filesystem::path{}; };
#else
    const auto path = [&](int i) { return i ? std::filesystem::path(argv[i]) : std::filesystem::path{}; };
#endif
    return MeterBillPaths{path(options.tariffArg), path(options.intervalsArg), path(archiveIndex),
                          path(options.ratesArg), path(options.invoiceArg)};
}

bool instant(const JsonValue *value, int64_t &result, bool allowEpoch = false) {
    uint64_t number = 0;
    if (!value || !value->asUint64(&number) || (!allowEpoch && !number) || number > meterBillMaxUtcMs) return false;
    result = static_cast<int64_t>(number);
    return true;
}

bool nonnegativeAmount(const JsonValue *value, ExactDecimal &amount) {
    return value && value->isString() && ExactDecimal::parse(value->text, &amount) &&
           amount.text() == value->text && !amount.negative() && amount.digits() <= 38 && amount.scale() <= 18;
}

const char *decodeIntervals(const JsonValue &intervals, const BillOpening &opening, int64_t cutoff,
                            const char *lateReadingError, std::vector<PowerInterval> &readings) {
    auto cursor = opening.throughUtcMs;
    readings.reserve(intervals.items.size());
    for (const auto &row : intervals.items) {
        PowerInterval reading;
        if (!row.isObject() || row.members.size() != 3 ||
            !instant(row.find("begin_utc_ms"), reading.beginUtcMs) ||
            !instant(row.find("end_utc_ms"), reading.endUtcMs) || reading.endUtcMs <= reading.beginUtcMs ||
            !nonnegativeAmount(row.find("average_watts"), reading.watts))
            return "invalid meter interval";
        if (reading.beginUtcMs != cursor) return "interval coverage has a gap or overlap";
        if (reading.endUtcMs > cutoff) return lateReadingError;
        cursor = reading.endUtcMs;
        readings.push_back(std::move(reading));
    }
    return nullptr;
}

const char *decodeRegister(const JsonValue &rows, const BillOpening &opening, int64_t cutoff,
                           std::vector<MeterImportInterval> &readings, ExactDecimal &start) {
    std::optional<int64_t> previousUtc;
    ExactDecimal previous;
    readings.reserve(rows.items.size() - 1);
    for (const auto &row : rows.items) {
        int64_t utc = 0;
        ExactDecimal cumulative;
        if (!row.isObject() || row.members.size() != 2 || !instant(row.find("utc_ms"), utc, true) ||
            !nonnegativeAmount(row.find("imported_kwh"), cumulative))
            return "invalid meter register reading";
        if (utc > cutoff) return "meter register reading is after the review cutoff";
        if (!previousUtc) {
            if (utc != opening.throughUtcMs) return "first meter register reading must match the saved opening time";
            start = cumulative;
        } else {
            if (utc <= *previousUtc) return "meter register timestamps must strictly increase";
            if (cumulative.compare(previous) < 0) return "meter register decreased (reset or rollover is unsupported)";
            ExactDecimal delta;
            if (!cumulative.subtract(previous, &delta)) return "meter register quantity exceeds arithmetic capacity";
            auto imported = electricityKwh(delta);
            if (!imported) return "meter register quantity exceeds arithmetic capacity";
            readings.push_back({*previousUtc, utc, std::move(*imported)});
        }
        previous = std::move(cumulative);
        previousUtc = utc;
    }
    return nullptr;
}

const char *decodeMeterReadings(const std::string &text, const TariffDeclaration &declaration, int64_t cutoff,
                               const char *lateReadingError, MeterReadings &readings,
                               const std::function<bool()> &stopped) {
    const auto &opening = declaration.opening;
    const bool annual = declaration.resetSchedule && declaration.resetSchedule->annualMonth;
    const bool extended = annual || declaration.plan.demandCharge.has_value();
    const auto byteLimit = extended ? maxMeterFileBytes : maxOrdinaryMeterFileBytes;
    const auto intervalLimit = extended ? maxAnnualMeterIntervals : maxOrdinaryMeterIntervals;
    if (text.size() > byteLimit) return "interval file unavailable, oversized or not a regular file";
    JsonValue root;
    if (!parseJsonStrict(text, &root, nullptr, {byteLimit, 5}) || !root.isObject())
        return "invalid interval document";
    const auto *schema = root.find("schema_version");
    const auto *scope = root.find("scope");
    uint64_t version = 0;
    if (!schema || !schema->asUint64(&version) || version < 1 || version > 3 || !scope || !scope->isString() ||
        root.members.size() != (version == 3 ? 5u : 3u))
        return "invalid interval document";
    const auto *rows = root.find(version == 1 ? "intervals" : version == 2 ? "register_readings" : "consumption");
    if (!rows || !rows->isArray()) return "invalid interval document";
    const auto size = rows->items.size();
    if (version == 2 ? size < 2 || size > intervalLimit + 1 : size < 1 || size > intervalLimit)
        return "invalid interval document";
    if (scope->text != opening.evidence.scope) return "meter scope differs from the saved opening";
    readings.version = version;
    if (version == 1)
        return decodeIntervals(*rows, opening, cutoff, lateReadingError,
                               readings.series.emplace<std::vector<PowerInterval>>());
    auto &imports = readings.series.emplace<std::vector<MeterImportInterval>>();
    if (version == 2) return decodeRegister(*rows, opening, cutoff, imports, readings.registerStart.emplace());

    int64_t end = 0;
    if (!instant(root.find("end_utc_ms"), end) || !instant(root.find("observed_utc_ms"), readings.octopusObserved) ||
        end <= opening.throughUtcMs || end > readings.octopusObserved)
        return "invalid Octopus consumption window or observation";
    if (readings.octopusObserved > cutoff) return "Octopus consumption observed after the review cutoff";
    if (declaration.plan.supplier != "Octopus Energy" || declaration.plan.currency != "GBP" ||
        declaration.plan.rounding != TariffRounding::MeterHalfHourKwhHalfEven2)
        return "Octopus consumption requires a declared Octopus Energy GBP tariff with half-hour rounding";
    return octopus_detail::decodeConsumption(*rows, opening.throughUtcMs, end, imports, stopped);
}

bool appendPeriod(std::string &report, unsigned period, const TariffDeclaration &declaration,
                  const BillOpening &opening, const BillAccrual &accrual, std::string &exact,
                  const std::optional<SeasonalCycleTariff> &seasonal) {
    ElectricityAmount usage;
    std::string usageText, change, total;
    if (!accrual.ending->importedKwh.numerator.subtract(opening.importedKwh.numerator, &usage.numerator) ||
        !usage.format(6, &usageText) || !accrual.provisionalChange->format(6, &change) ||
        !accrual.provisionalTotal->format(6, &total)) return false;

    const auto &currency = declaration.plan.currency;
    report += "Cycle " + std::to_string(period) + " [" + std::to_string(opening.cycleBeginUtcMs) + ", " +
              std::to_string(opening.cycleEndUtcMs) + ") UTC ms\n";
    report += "  Reviewed interval range [" + std::to_string(opening.throughUtcMs) + ", " +
              std::to_string(accrual.ending->throughUtcMs) + ") UTC ms\n";
    report += "  Imported usage " + usageText + " kWh\n";
    if (declaration.resetSchedule && declaration.resetSchedule->bimonthlyStartMonth) {
        report += "  Rate basis is monthly average across the two-month cycle.\n";
        if (accrual.settledTotal) {
            ElectricityAmount average;
            std::string averageText;
            if (!accrual.ending->importedKwh.numerator.divideExact(2, &average.numerator) ||
                !average.format(6, &averageText)) return false;
            report += "  Monthly-average usage at closing " + averageText + " kWh\n";
        }
    }
    if (seasonal) report += "  Seasonal allowance for cycle " + seasonal->allowanceKwh.text() + " kWh\n";
    ElectricityAmount demandSubtotal,billablePeak,ratchetFloor;
    if (const auto &demand=declaration.plan.demandCharge) {
        std::string peak, amount;
        if (!billableDemand(*demand,*accrual.ending,billablePeak,ratchetFloor) ||
            !accrual.ending->peakDemandKw->format(6,&peak) ||
            !demand->charge(billablePeak,&demandSubtotal) ||
            !demandSubtotal.format(6,&amount)) return false;
        report += "  Peak demand " + peak + " kW over " + std::to_string(demand->windowMinutes) + " minute windows\n";
        if (demand->ratchet) {
            std::string floor,billable;
            if (!ratchetFloor.format(6,&floor) || !billablePeak.format(6,&billable)) return false;
            report += "  Demand ratchet floor " + floor + " kW\n";
            report += "  Billable demand " + billable + " kW\n";
        }
        report += "  Demand charge before subtotal multiplier " + amount + " " + currency + "\n";
    }
    if (accrual.billedKwh) {
        std::string billed;
        if (!accrual.billedKwh->format(6, &billed)) return false;
        report += "  Billed usage " + billed + " kWh (rounded per half hour)\n";
    }
    report += "  Change from imported readings " + change + " " + currency + "\n";
    report += accrual.settledTotal ? "  Cycle estimate at closing " : "  Provisional cycle estimate ";
    report += total + " " + currency;
    if (opening.throughUtcMs != opening.cycleBeginUtcMs) report += " (includes declared opening)";
    report += '\n';
    if (accrual.settlementDependent) report += "  Closing charges depend on full-cycle usage.\n";
    const auto &ending = *accrual.ending;
    exact += "[" + std::to_string(opening.cycleBeginUtcMs) + "," + std::to_string(opening.cycleEndUtcMs) +
              "," + std::to_string(opening.throughUtcMs) + "," + std::to_string(ending.throughUtcMs) +
              "," + jsonQuote(ending.timeDatabaseVersion) + "," + jsonQuote(usage.numerator.text()) +
              "," + jsonQuote(ending.importedKwh.numerator.text()) + "," + jsonQuote(ending.accruedSubtotal.numerator.text()) +
              "," + jsonQuote(accrual.provisionalChange->numerator.text()) + "," + jsonQuote(accrual.provisionalTotal->numerator.text()) +
              "," + (accrual.settledTotal ? jsonQuote(accrual.settledTotal->numerator.text()) : "null") +
              (accrual.settlementDependent ? ",true" : ",false");
    if (accrual.billedKwh) exact += "," + jsonQuote(accrual.billedKwh->numerator.text());
    if (seasonal) exact += "," + jsonQuote(seasonal->allowanceKwh.text());
    if (ending.peakDemandKw)
        exact += "," + jsonQuote(ending.peakDemandKw->numerator.text()) + "," + jsonQuote(demandSubtotal.numerator.text());
    if (declaration.plan.demandCharge && declaration.plan.demandCharge->ratchet)
        exact += "," + jsonQuote(ratchetFloor.numerator.text()) + "," + jsonQuote(billablePeak.numerator.text());
    exact += "]\n";
    return true;
}

template<class Interval>
const char *reviewReadings(const std::vector<Interval> &readings, const TariffDeclaration &declaration, int64_t cutoff,
                           const std::function<bool()> &stopped, std::string &report, std::string &exactResults,
                           const MeterInvoice *invoice, std::string *exactInvoice,
                           std::string *meterOnlyExact = nullptr) {
    constexpr bool isRegister = std::is_same_v<Interval, MeterImportInterval>;
    using Trace = std::conditional_t<isRegister, MeterImportTrace, PowerTrace>;
    auto opening = declaration.opening;
    size_t next = 0;
    auto cursor = opening.throughUtcMs;
    unsigned cycle = 1;
    for (; next < readings.size(); ++cycle) {
        if (stopped()) return "review cancelled";
        if (cycle > maxTariffComparisonCycles) return "intervals span more than 12 cycles";
        Trace slice{opening.evidence, {}};
        while (next < readings.size() && cursor < opening.cycleEndUtcMs) {
            const auto &reading = readings[next];
            const auto end = (std::min)(reading.endUtcMs, opening.cycleEndUtcMs);
            if constexpr (isRegister) {
                if (end != reading.endUtcMs) return "meter register needs a reading at the billing-cycle boundary";
                slice.intervals.push_back(reading);
            } else {
                slice.intervals.push_back({cursor, end, reading.watts});
            }
            cursor = end;
            if (cursor == reading.endUtcMs) ++next;
        }
        std::optional<SeasonalCycleTariff> seasonal;
        const auto status = seasonalTariffForCycle(declaration, opening, cutoff, seasonal);
        if (status != ElectricityStatus::Ok) return electricityStatusName(status);
        const auto &plan = seasonal ? seasonal->plan : declaration.plan;
        BillAccrual accrual;
        const auto allocation = declaration.dailyFeeAllocation();
        if constexpr (isRegister)
            accrual = accrueMeterImport(plan, opening, slice, cursor, cutoff, stopped, allocation);
        else
            accrual = accrueBill(plan, opening, slice, cursor, cutoff, stopped, allocation);
        if (stopped()) return "review cancelled";
        if (accrual.status != ElectricityStatus::Ok) return electricityStatusName(accrual.status);
        const auto exactStart = exactResults.size();
        if (!appendPeriod(report, cycle, declaration, opening, accrual, exactResults, seasonal))
            return "amount exceeds the display range";
        if (meterOnlyExact) meterOnlyExact->append(exactResults, exactStart);
        if (invoice) {
            if (const auto *problem = appendInvoiceComparison(*invoice, cycle - 1, opening, accrual,
                                                              declaration.plan.currency, report, exactInvoice))
                return problem;
        }
        if (next < readings.size()) {
            opening = *accrual.ending;
            bool chargedDaySplit = false;
            if (advanceTariffOpening(declaration, opening, cutoff, chargedDaySplit) != ElectricityStatus::Ok)
                return chargedDaySplit ? "a reset splits a tariff day with a daily fee" :
                                         "next cycle unavailable under the saved reset rule";
        }
    }
    if (invoice && cycle - 1 != invoice->cycles.size())
        return "invoice must list one charge for each reviewed cycle";
    return nullptr;
}

struct MeterReview {
    TariffDeclaration declaration;
    MeterReadings readings;
    std::string report, exact;
};

const char *prepareMeterReview(const ReviewOriginal &original, int64_t now, bool showCutoff,
                               const char *lateReadingError, const std::function<bool()> &stopped,
                               MeterReview &review) {
    JsonValue root;
    auto &declaration = review.declaration;
    if (stopped()) return "review cancelled";
    if (!parseJsonStrict(original.tariff, &root, nullptr))
        return "saved tariff declaration unavailable or invalid";
    const auto *schema = root.find("schema_version");
    uint64_t schemaVersion = 0;
    const auto usesNamedClock = [](const JsonValue *part) {
        const auto *clock = part ? part->find("clock") : nullptr;
        const auto *kind = clock ? clock->find("kind") : nullptr;
        return kind && kind->isString() && kind->text == "named_iana";
    };
    const bool namedTariff = schema && schema->asUint64(&schemaVersion) &&
        (schemaVersion == 4 || (schemaVersion >= 5 && schemaVersion <= 10 &&
         (usesNamedClock(root.find("plan")) || usesNamedClock(root.find("reset_policy")))));
    if (namedTariff && configureBundledCivilTimeData() != CivilStatus::Ok)
        return "reviewed tariff time database unavailable or invalid";
    if (!decodeTariffDeclaration(root, &declaration)) return "saved tariff declaration unavailable or invalid";
    if (now < 1 || now > meterBillMaxUtcMs || static_cast<int64_t>(declaration.confirmedUtc * 1000) > now)
        return "local clock precedes the review or is unavailable";
    const auto cutoff = original.cutoff;
    if (cutoff > now) return "review cutoff is in the future";
    if (declaration.plan.demandCharge && declaration.plan.demandCharge->ratchet &&
        declaration.opening.cycleBeginUtcMs > cutoff)
        return "completed demand history is after the review cutoff";
    if (stopped()) return "review cancelled";
    if (const auto *problem = decodeMeterReadings(original.intervals, declaration, cutoff, lateReadingError,
                                                   review.readings, stopped))
        return problem;

    auto &report = review.report;
    report = std::holds_alternative<std::vector<MeterImportInterval>>(review.readings.series)
        ? "Estimated electricity costs from a declared tariff and user-supplied meter register readings\n"
          "Uses exact differences between cumulative import readings in kWh.\n"
        : "Estimated electricity costs from a declared tariff and user-supplied meter intervals\n"
          "Average import is applied uniformly within each interval.\n";
    if (review.readings.octopusObserved) {
        report = "Estimated electricity costs from declared Octopus Energy whole-meter electricity imports\n";
        report += "API quantities are reported to 0.001 kWh. Local bundle observation " +
            std::to_string(review.readings.octopusObserved) + " UTC ms.\n";
    }
    if (namedTariff) report += std::string("Reviewed IANA time database ") + tariffTzdbVersion + "\n";
    if (declaration.componentMultipliers)
        report += "Component multipliers apply to the declared part of the unit rate.\n";
    if (showCutoff) report += "Review cutoff " + std::to_string(cutoff) + " UTC ms\n";
    if (declaration.dailyFeeAllocation() == DailyFeeAllocation::ProrateByCycleMinutes)
        report += "Daily fees use the civil-midnight rate, split by minutes in each billing cycle.\n";
    if (declaration.plan.rounding == TariffRounding::MeterHalfHourKwhHalfEven2) {
        if (!std::holds_alternative<std::vector<MeterImportInterval>>(review.readings.series))
            return "half-hour rounding requires cumulative whole-meter register readings or Octopus imports";
        report += "Billing rounds each complete UTC half hour to 0.01 kWh, ties to even, before applying the rate.\n";
    }
    if (!original.supplierRates.empty()) {
        OctopusRateImport imported;
        if (const auto *problem = importOctopusRates(original.supplierRates, cutoff, declaration, imported, stopped))
            return problem;
        report += std::string(imported.go ? "Octopus Go" : "Octopus Agile") +
            " public rate snapshot (" + imported.paymentMethod + ")\n";
        report += "  Observed " + std::to_string(imported.observedUtcMs) + " UTC ms, " +
                  std::to_string(imported.intervals) +
                  (imported.go ? " published price intervals including VAT.\n" : " half-hour prices including VAT.\n");
    }

    return nullptr;
}

const char *calculateMeterReview(const ReviewOriginal &original, bool includeInvoiceInExact,
                                 const std::function<bool()> &stopped, MeterReview &review,
                                 std::string *meterOnlyExact = nullptr) {
    std::optional<MeterInvoice> invoice;
    if (!original.invoice.empty()) {
        if (const auto *problem = decodeMeterInvoice(original.invoice, review.declaration, invoice.emplace()))
            return problem;
        review.report += invoice->annual ? "Annual reconciliation uses " : "Invoice comparison uses ";
        review.report += std::to_string(invoice->places) + " decimal places with ties ";
        review.report += invoice->ties == DecimalTies::ToEven ? "rounded to even.\n" : "rounded away from zero.\n";
    }
    return std::visit([&](const auto &series) {
        return reviewReadings(series, review.declaration, original.cutoff, stopped, review.report, review.exact,
                              invoice ? &*invoice : nullptr, includeInvoiceInExact ? &review.exact : nullptr,
                              meterOnlyExact);
    }, review.readings.series);
}

// Bind the previous meter observations as well as their exact accrued amounts. Absolute register
// readings are determined by the first reading and the exact adjacent differences.
template<class Interval>
std::string meterPrefixDigest(const MeterReadings &readings, const std::vector<Interval> &series,
                              const std::string &meterExact) {
    std::string original = std::to_string(readings.version) + '\n';
    if (readings.registerStart) original += readings.registerStart->text() + '\n';
    for (const auto &interval : series) {
        original += std::to_string(interval.beginUtcMs) + ' ' + std::to_string(interval.endUtcMs) + ' ';
        if constexpr (std::is_same_v<Interval, MeterImportInterval>)
            original += interval.importedKwh.numerator.text();
        else
            original += interval.watts.text();
        original += '\n';
    }
    return sha256Hex(original + meterExact);
}

const char *verifyMeterPrefix(const ReviewOriginal &original, const MeterReview &review,
                              const std::function<bool()> &stopped) {
    if (!original.previous) return nullptr;
    const auto &previous = *original.previous;
    return std::visit([&](const auto &series) -> const char * {
        const auto last = std::lower_bound(series.begin(), series.end(), previous.through,
            [](const auto &interval, int64_t through) { return interval.endUtcMs < through; });
        if (last == series.end() || last->endUtcMs != previous.through || last + 1 == series.end())
            return "new readings must extend the saved meter coverage";
        using Series = std::decay_t<decltype(series)>;
        const Series prefix(series.begin(), last + 1);
        std::string ignored, prefixExact;
        if (const auto *problem = reviewReadings(prefix, review.declaration, previous.cutoff, stopped, ignored,
                                                 prefixExact, nullptr, nullptr)) return problem;
        if (meterPrefixDigest(review.readings, prefix, prefixExact) != previous.prefixSha256)
            return "new readings or exact charges differ from the saved review prefix";
        if (original.cutoff != previous.cutoff) {
            ignored.clear();
            std::string currentExact;
            if (const auto *problem = reviewReadings(prefix, review.declaration, original.cutoff, stopped, ignored,
                                                     currentExact, nullptr, nullptr)) return problem;
            if (currentExact != prefixExact) return "the new cutoff would change the saved review charges";
        }
        return nullptr;
    }, review.readings.series);
}

int refuse(const char *reason) {
    std::fprintf(stderr, "meter-bill: %s\n", reason);
    return 3;
}

}  // namespace

int commandMeterBill(const MeterBillOptions &options, int argc, const char *const *argv, const volatile sig_atomic_t *stop) {
    if (options.help) {
        std::puts("canary-miner meter-bill --tariff FILE --intervals FILE [--octopus-rates FILE] [--as-of-utc-ms MS]\n"
                  "    [--invoice FILE] [--save-review NAME.cmr]\n"
                  "canary-miner meter-bill --open-review FILE [--invoice FILE]\n"
                  "canary-miner meter-bill --extend-review FILE --intervals FILE --save-review NAME.cmr\n"
                  "    [--as-of-utc-ms MS] [--invoice FILE]\n"
                  "Review a saved combined tariff against contiguous whole-meter intervals.\n"
                  "Accepts average-watt intervals or cumulative whole-meter import readings. Prints an estimate to stdout.\n"
                  "Also accepts local Octopus electricity import bundles in schema version 3.\n"
                  "Register readings need billing-cycle boundaries and any energy-rate boundaries that split nonzero use.\n"
                  "Half-hour rounding uses complete UTC half hours. Without demand, give one reading per boundary.\n"
                  "With demand, finer readings are grouped. Add a reading wherever either grid splits nonzero import.\n"
                  "Demand requires complete aligned windows. Add a register reading wherever a demand boundary splits nonzero import.\n"
                  "Use --octopus-rates to apply captured public Agile or Go prices to a compatible tariff declaration.\n"
                  "Use --as-of-utc-ms to require readings and published rates available by that UTC instant.\n"
                  "Use --invoice to compare supply charges or reconcile interim charges against a complete annual estimate.\n"
                  "Save optionally writes the private inputs and exact results to a new archive in the current folder.\n"
                  "Open rechecks the saved inputs and results at their original cutoff without writing files.\n"
                  "Extend verifies the saved review and requires the complete interval history with additional readings.");
        return 0;
    }
    const std::function<bool()> stopped = [stop] { return stop && *stop; };
    const auto stopResult = [&]() {
        if (!options.saveArg && !options.archiveArg && !options.invoiceArg) return 0;
        std::fputs(options.saveArg ? "meter-bill: archive save stopped before publication\n" :
                   options.archiveArg ? "meter-bill: saved review verification stopped\n" :
                                        "meter-bill: invoice comparison stopped\n", stderr);
        return 130;
    };
    const auto fail = [&](const char *reason) { return stopped() ? stopResult() : refuse(reason); };
    try {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (stopped()) return stopResult();
        const auto paths = meterBillPaths(options, argc, argv);
        if (!paths) return fail(options.reviewCutoffArg ? "invalid review cutoff or input file arguments" :
                                                         "could not resolve input file arguments");
        ReviewOriginal original;
        const bool reopening = options.archiveArg != 0;
        const bool extending = options.extendArg != 0;
        const bool archiving = options.saveArg != 0;
        const char *lateReadingError = options.reviewCutoffArg || reopening || extending
            ? "interval ends after the review cutoff" : "interval ends in the future";
        int64_t previousConsumptionObservation = 0;
        if (reopening || extending) {
            std::string bytes;
            if (!readLocalReviewInput(paths->archive, maxArchiveBytes, bytes) || !decodeReviewOriginal(bytes, original))
                return fail("review archive unavailable or invalid");
            if (extending) {
                const auto cutoff = options.reviewCutoffArg ? options.reviewCutoffUtcMs : now;
                if (cutoff < original.cutoff) return fail("new review cutoff precedes the saved cutoff");
                MeterReview earlier;
                if (const auto *problem = prepareMeterReview(original, now, false, lateReadingError, stopped, earlier))
                    return fail(problem);
                if (const auto *problem = verifyMeterPrefix(original, earlier, stopped)) return fail(problem);
                std::string meterExact;
                if (const auto *problem = calculateMeterReview(original, true, stopped, earlier, &meterExact))
                    return fail(problem);
                if (original.results != earlier.exact)
                    return fail("saved review amounts differ from the recalculated amounts");
                auto &previous = original.previous.emplace();
                previous.cutoff = original.cutoff;
                previous.archiveSha256 = sha256Hex(bytes);
                std::visit([&](const auto &series) {
                    previous.through = series.back().endUtcMs;
                    previous.prefixSha256 = meterPrefixDigest(earlier.readings, series, meterExact);
                }, earlier.readings.series);
                previousConsumptionObservation = earlier.readings.octopusObserved;
                original.cutoff = cutoff;
                original.invoice.clear();
            }
        } else {
            original.cutoff = options.reviewCutoffArg ? options.reviewCutoffUtcMs : now;
            if (!readLocalReviewInput(paths->tariff, tariffDeclarationMaxBytes, original.tariff))
                return fail("saved tariff declaration unavailable or invalid");
        }
        if (stopped()) return stopResult();
        if (!reopening && !readLocalReviewInput(paths->intervals, maxMeterFileBytes, original.intervals))
            return fail("interval file unavailable, oversized or not a regular file");
        if (options.ratesArg &&
            (!readLocalReviewInput(paths->rates, maxOctopusRateFileBytes, original.supplierRates) ||
             original.supplierRates.empty()))
            return fail("Octopus rate file unavailable, empty, oversized or not a regular file");

        MeterReview review;
        if (const auto *problem = prepareMeterReview(original, now, options.reviewCutoffArg || reopening || archiving,
                                                      lateReadingError, stopped, review)) return fail(problem);
        if (review.readings.octopusObserved < previousConsumptionObservation)
            return fail("new Octopus consumption observation precedes the saved observation");
        if (const auto *problem = verifyMeterPrefix(original, review, stopped)) return fail(problem);
        if (original.previous)
            review.report += "Extends verified meter coverage through " + std::to_string(original.previous->through) +
                             " UTC ms (previous archive SHA-256 " + original.previous->archiveSha256 + ")\n";
        const bool retainedInvoice = !original.invoice.empty();
        if (options.invoiceArg) {
            if (retainedInvoice) return fail("saved review already contains an invoice comparison");
            if (!readLocalReviewInput(paths->invoice, maxInvoiceBytes, original.invoice) || original.invoice.empty())
                return fail("invoice file unavailable, empty, oversized or not a regular file");
        }
        if (const auto *problem = calculateMeterReview(original, archiving || retainedInvoice, stopped, review))
            return fail(problem);
        if (stopped()) return stopResult();
        if (reopening && original.results != review.exact) return fail("saved review amounts differ from the recalculated amounts");
        if (archiving) {
            if (review.exact.size() > maxExactResultsBytes) return fail("exact results exceed the archive bound");
            original.results = std::move(review.exact);
            const auto state = local_review_archive::save(options.archiveName, ".cmr", encodeReviewOriginal(original),
                                                          maxArchiveBytes, stop);
            if (state == local_review_archive::ReviewSave::Stopped) return stopResult();
            if (state == local_review_archive::ReviewSave::Uncertain)
                return refuse("archive publication or cleanup is uncertain; inspect the review and its temporary file");
            if (state != local_review_archive::ReviewSave::Written)
                return refuse("could not create the new review archive; any existing archive was preserved");
        }
        if (std::fwrite(review.report.data(), 1, review.report.size(), stdout) != review.report.size() ||
            std::fflush(stdout) != 0)
            return 1;
        return 0;
    } catch (...) {
        return fail("review could not be completed");
    }
}

}  // namespace om
