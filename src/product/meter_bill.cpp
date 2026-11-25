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
constexpr size_t maxMeterIntervals = 8192;

using MeterReadings = std::variant<std::vector<PowerInterval>, std::vector<MeterImportInterval>>;
struct MeterBillPaths { std::filesystem::path tariff, intervals, archive, rates, invoice; };

std::optional<MeterBillPaths> meterBillPaths(const MeterBillOptions &options, int argc, const char *const *argv) {
    const auto validIndex = [argc](int index) { return index >= 3 && index < argc; };
    if (options.archiveArg ? !validIndex(options.archiveArg)
                           : !validIndex(options.tariffArg) || !validIndex(options.intervalsArg))
        return std::nullopt;
    if ((options.reviewCutoffArg && !validIndex(options.reviewCutoffArg)) ||
        (options.saveArg && !validIndex(options.saveArg)) ||
        (options.ratesArg && !validIndex(options.ratesArg)) ||
        (options.invoiceArg && !validIndex(options.invoiceArg)))
        return std::nullopt;
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
        return std::wstring_view(args.values[index-1]) == flag;
    };
    if (options.invoiceArg && !matches(options.invoiceArg, L"--invoice")) return std::nullopt;
    std::filesystem::path invoice;
    if (options.invoiceArg) invoice = args.values[options.invoiceArg];
    if (options.archiveArg) {
        if (!matches(options.archiveArg, L"--open-review")) return std::nullopt;
        return MeterBillPaths{{}, {}, args.values[options.archiveArg], {}, std::move(invoice)};
    }
    if (!matches(options.tariffArg, L"--tariff") || !matches(options.intervalsArg, L"--intervals"))
        return std::nullopt;
    if (options.reviewCutoffArg &&
        (!matches(options.reviewCutoffArg, L"--as-of-utc-ms") ||
         std::wstring_view(args.values[options.reviewCutoffArg]) != std::to_wstring(options.reviewCutoffUtcMs)))
        return std::nullopt;
    if (options.saveArg &&
        (!matches(options.saveArg, L"--save-review") ||
         std::wstring_view(args.values[options.saveArg]) != std::wstring(options.archiveName.begin(), options.archiveName.end())))
        return std::nullopt;
    if (options.ratesArg && !matches(options.ratesArg, L"--octopus-rates"))
        return std::nullopt;
    return MeterBillPaths{args.values[options.tariffArg], args.values[options.intervalsArg], {},
                          options.ratesArg ? std::filesystem::path(args.values[options.ratesArg]) : std::filesystem::path{},
                          std::move(invoice)};
#else
    const std::filesystem::path invoice = options.invoiceArg ? argv[options.invoiceArg] : "";
    if (options.archiveArg) return MeterBillPaths{{}, {}, argv[options.archiveArg], {}, invoice};
    return MeterBillPaths{argv[options.tariffArg], argv[options.intervalsArg], {},
                          options.ratesArg ? std::filesystem::path(argv[options.ratesArg]) : std::filesystem::path{}, invoice};
#endif
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
                           std::vector<MeterImportInterval> &readings) {
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
                               const char *lateReadingError, MeterReadings &readings, int64_t &octopusObserved,
                               const std::function<bool()> &stopped) {
    const auto &opening = declaration.opening;
    JsonValue root;
    if (!parseJsonStrict(text, &root, nullptr, {maxMeterFileBytes, 5}) || !root.isObject())
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
    if (version == 2 ? size < 2 || size > maxMeterIntervals + 1 : size < 1 || size > maxMeterIntervals)
        return "invalid interval document";
    if (scope->text != opening.evidence.scope) return "meter scope differs from the saved opening";
    if (version == 1)
        return decodeIntervals(*rows, opening, cutoff, lateReadingError, readings.emplace<std::vector<PowerInterval>>());
    auto &imports = readings.emplace<std::vector<MeterImportInterval>>();
    if (version == 2) return decodeRegister(*rows, opening, cutoff, imports);

    int64_t end = 0;
    if (!instant(root.find("end_utc_ms"), end) || !instant(root.find("observed_utc_ms"), octopusObserved) ||
        end <= opening.throughUtcMs || end > octopusObserved)
        return "invalid Octopus consumption window or observation";
    if (octopusObserved > cutoff) return "Octopus consumption observed after the review cutoff";
    if (declaration.plan.supplier != "Octopus Energy" || declaration.plan.currency != "GBP" ||
        declaration.plan.rounding != TariffRounding::MeterHalfHourKwhHalfEven2)
        return "Octopus consumption requires a declared Octopus Energy GBP tariff with half-hour rounding";
    return octopus_detail::decodeConsumption(*rows, opening.throughUtcMs, end, imports, stopped);
}

bool appendPeriod(std::string &report, unsigned period, const TariffDeclaration &declaration,
                  const BillOpening &opening, const BillAccrual &accrual, std::string *exact) {
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
    if (exact) {
        const auto &ending = *accrual.ending;
        *exact += "[" + std::to_string(opening.cycleBeginUtcMs) + "," + std::to_string(opening.cycleEndUtcMs) +
                  "," + std::to_string(opening.throughUtcMs) + "," + std::to_string(ending.throughUtcMs) +
                  "," + jsonQuote(ending.timeDatabaseVersion) + "," + jsonQuote(usage.numerator.text()) +
                  "," + jsonQuote(ending.importedKwh.numerator.text()) + "," + jsonQuote(ending.accruedSubtotal.numerator.text()) +
                  "," + jsonQuote(accrual.provisionalChange->numerator.text()) + "," + jsonQuote(accrual.provisionalTotal->numerator.text()) +
                  "," + (accrual.settledTotal ? jsonQuote(accrual.settledTotal->numerator.text()) : "null") +
                  (accrual.settlementDependent ? ",true" : ",false");
        if (accrual.billedKwh) *exact += "," + jsonQuote(accrual.billedKwh->numerator.text());
        *exact += "]\n";
    }
    return true;
}

template<class Interval>
const char *reviewReadings(const std::vector<Interval> &readings, const TariffDeclaration &declaration, int64_t cutoff,
                           const std::function<bool()> &stopped, std::string &report, std::string *exactResults,
                           const MeterInvoice *invoice, std::string *exactInvoice) {
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
        BillAccrual accrual;
        const auto allocation = declaration.dailyFeeAllocation();
        if constexpr (isRegister)
            accrual = accrueMeterImport(declaration.plan, opening, slice, cursor, cutoff, stopped, allocation);
        else
            accrual = accrueBill(declaration.plan, opening, slice, cursor, cutoff, stopped, allocation);
        if (stopped()) return "review cancelled";
        if (accrual.status != ElectricityStatus::Ok) return electricityStatusName(accrual.status);
        if (!appendPeriod(report, cycle, declaration, opening, accrual, exactResults))
            return "amount exceeds the display range";
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
                  "Review a saved combined tariff against contiguous whole-meter intervals.\n"
                  "Accepts average-watt intervals or cumulative whole-meter import readings. Prints an estimate to stdout.\n"
                  "Also accepts local Octopus electricity import bundles in schema version 3.\n"
                  "Register readings need billing-cycle boundaries and any energy-rate boundaries that split nonzero use.\n"
                  "Half-hour rounding requires a reading for every UTC half hour.\n"
                  "Use --octopus-rates to apply captured public Agile or Go prices to a compatible tariff declaration.\n"
                  "Use --as-of-utc-ms to require readings and published rates available by that UTC instant.\n"
                  "Use --invoice to compare declared supply charges for complete billing cycles.\n"
                  "Save optionally writes the private inputs and exact results to a new archive in the current folder.\n"
                  "Open rechecks the saved inputs and results at their original cutoff without writing files.");
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
        const bool archiving = options.saveArg != 0;
        if (reopening) {
            std::string bytes;
            if (!readLocalReviewInput(paths->archive, maxArchiveBytes, bytes) || !decodeReviewOriginal(bytes, original))
                return fail("review archive unavailable or invalid");
        } else {
            original.cutoff = options.reviewCutoffArg ? options.reviewCutoffUtcMs : now;
            if (!readLocalReviewInput(paths->tariff, tariffDeclarationMaxBytes, original.tariff))
                return fail("saved tariff declaration unavailable or invalid");
        }

        JsonValue root;
        TariffDeclaration declaration;
        if (stopped()) return stopResult();
        if (!parseJsonStrict(original.tariff, &root, nullptr))
            return fail("saved tariff declaration unavailable or invalid");
        const auto *schema = root.find("schema_version");
        uint64_t schemaVersion = 0;
        const bool namedTariff = schema && schema->asUint64(&schemaVersion) && schemaVersion == 4;
        if (namedTariff && configureBundledCivilTimeData() != CivilStatus::Ok)
            return fail("reviewed tariff time database unavailable or invalid");
        if (!decodeTariffDeclaration(root, &declaration)) return fail("saved tariff declaration unavailable or invalid");
        if (now < 1 || now > meterBillMaxUtcMs || static_cast<int64_t>(declaration.confirmedUtc * 1000) > now)
            return fail("local clock precedes the review or is unavailable");
        const auto cutoff = original.cutoff;
        if (cutoff > now) return fail("review cutoff is in the future");
        const char *lateReadingError = options.reviewCutoffArg || reopening ? "interval ends after the review cutoff" :
                                                                           "interval ends in the future";
        if (stopped()) return stopResult();
        if (!reopening && !readLocalReviewInput(paths->intervals, maxMeterFileBytes, original.intervals))
            return fail("interval file unavailable, oversized or not a regular file");
        MeterReadings readings;
        int64_t octopusObserved = 0;
        if (const char *problem = decodeMeterReadings(original.intervals, declaration, cutoff, lateReadingError,
                                                      readings, octopusObserved, stopped))
            return fail(problem);

        std::string report = std::holds_alternative<std::vector<MeterImportInterval>>(readings)
            ? "Estimated electricity costs from a declared tariff and user-supplied meter register readings\n"
              "Uses exact differences between cumulative import readings in kWh.\n"
            : "Estimated electricity costs from a declared tariff and user-supplied meter intervals\n"
              "Average import is applied uniformly within each interval.\n";
        if (octopusObserved) {
            report = "Estimated electricity costs from declared Octopus Energy whole-meter electricity imports\n";
            report += "API quantities are reported to 0.001 kWh. Local bundle observation " +
                std::to_string(octopusObserved) + " UTC ms.\n";
        }
        if (namedTariff) report += std::string("Reviewed IANA time database ") + tariffTzdbVersion + "\n";
        if (options.reviewCutoffArg || reopening || archiving) report += "Review cutoff " + std::to_string(cutoff) + " UTC ms\n";
        if (declaration.dailyFeeAllocation() == DailyFeeAllocation::ProrateByCycleMinutes)
            report += "Daily fees use the civil-midnight rate, split by minutes in each billing cycle.\n";
        if (declaration.plan.rounding == TariffRounding::MeterHalfHourKwhHalfEven2) {
            if (!std::holds_alternative<std::vector<MeterImportInterval>>(readings))
                return fail("half-hour rounding requires cumulative whole-meter register readings or Octopus imports");
            report += "Billing rounds each complete UTC half hour to 0.01 kWh, ties to even, before applying the rate.\n";
        }
        if (options.ratesArg &&
            (!readLocalReviewInput(paths->rates, maxOctopusRateFileBytes, original.supplierRates) ||
             original.supplierRates.empty()))
            return fail("Octopus rate file unavailable, empty, oversized or not a regular file");
        if (!original.supplierRates.empty()) {
            OctopusRateImport imported;
            if (const auto *problem = importOctopusRates(original.supplierRates, cutoff, declaration, imported, stopped))
                return fail(problem);
            report += std::string(imported.go ? "Octopus Go" : "Octopus Agile") +
                " public rate snapshot (" + imported.paymentMethod + ")\n";
            report += "  Observed " + std::to_string(imported.observedUtcMs) + " UTC ms, " +
                      std::to_string(imported.intervals) +
                      (imported.go ? " published price intervals including VAT.\n" : " half-hour prices including VAT.\n");
        }

        const bool retainedInvoice = !original.invoice.empty();
        if (options.invoiceArg) {
            if (retainedInvoice) return fail("saved review already contains an invoice comparison");
            if (!readLocalReviewInput(paths->invoice, maxInvoiceBytes, original.invoice) || original.invoice.empty())
                return fail("invoice file unavailable, empty, oversized or not a regular file");
        }
        std::optional<MeterInvoice> invoice;
        if (!original.invoice.empty()) {
            if (const auto *problem = decodeMeterInvoice(original.invoice, declaration, invoice.emplace()))
                return fail(problem);
            report += "Invoice comparison uses " + std::to_string(invoice->places) + " decimal places with ties ";
            report += invoice->ties == DecimalTies::ToEven ? "rounded to even.\n" : "rounded away from zero.\n";
        }

        std::string exact;
        auto *exactResults = archiving || reopening ? &exact : nullptr;
        auto *exactInvoice = archiving || retainedInvoice ? exactResults : nullptr;
        if (const auto *problem = std::visit([&](const auto &series) {
            return reviewReadings(series, declaration, cutoff, stopped, report, exactResults,
                                  invoice ? &*invoice : nullptr, exactInvoice);
        }, readings)) return fail(problem);
        if (stopped()) return stopResult();
        if (reopening && original.results != exact) return fail("saved review amounts differ from the recalculated amounts");
        if (archiving) {
            if (exact.size() > maxExactResultsBytes) return fail("exact results exceed the archive bound");
            original.results = std::move(exact);
            const auto state = local_review_archive::save(options.archiveName, ".cmr", encodeReviewOriginal(original),
                                                          maxArchiveBytes, stop);
            if (state == local_review_archive::ReviewSave::Stopped) return stopResult();
            if (state == local_review_archive::ReviewSave::Uncertain)
                return refuse("archive publication or cleanup is uncertain; inspect the review and its temporary file");
            if (state != local_review_archive::ReviewSave::Written)
                return refuse("could not create the new review archive; any existing archive was preserved");
        }
        if (std::fwrite(report.data(), 1, report.size(), stdout) != report.size() || std::fflush(stdout) != 0)
            return 1;
        return 0;
    } catch (...) {
        return fail("review could not be completed");
    }
}

}  // namespace om
