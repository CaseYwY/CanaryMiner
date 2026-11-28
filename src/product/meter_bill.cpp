#include "meter_bill.h"
#include "meter_bill_archive.h"
#include "meter_bill_input.h"
#include "local_store_lease.h"
#include "meter_prepaid.h"
#include "meter_invoice.h"
#include "market_public_sample.h"
#include "octopus_meter_consumption.h"
#include "octopus_meter_rates.h"
#include "public_https.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>
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
    std::optional<ExactDecimal> registerStart, registerEnd, secondaryRegisterStart;
    std::vector<std::string> apparentSources;
    int64_t octopusObserved = 0;
    uint64_t version = 0;
};

struct MeterBillPaths {
    std::filesystem::path tariff, intervals, archive, rates, invoice, prepaid, previousCycle, importPages;
};

std::optional<MeterBillPaths> meterBillPaths(const MeterBillOptions &options, int argc, const char *const *argv) {
    const auto archiveIndex = options.recurringModeArg ? options.recurringModeArg :
        options.rollForwardArg ? options.rollForwardArg :
        options.prepareCycleArg ? options.prepareCycleArg : options.archiveArg ? options.archiveArg :
        options.nextCycleArg ? options.nextCycleArg : options.extendArg;
    for (int index : {archiveIndex, options.tariffArg, options.intervalsArg, options.reviewCutoffArg,
                      options.saveArg, options.ratesArg, options.invoiceArg, options.prepaidArg, options.previousCycleArg,
                      options.saveTariffArg, options.saveRegisterArg, options.dnsServerArg, options.importPagesArg,
                      options.saveReadingsArg, options.watchArg})
        if (index && (index < 3 || index >= argc)) return std::nullopt;
    for (int index : {options.allowPublicRatesArg, options.confirmImportArg, options.quietWatchArg,
                      options.historyInvoicesArg, options.watchInvoicesArg})
        if (index && (index < 2 || index >= argc)) return std::nullopt;
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
    const auto recurringFlag = options.recurringMode == RecurringMode::ReconcileInvoices ? L"--reconcile-invoices" :
        options.recurringMode == RecurringMode::History ? L"--cycle-history" :
        options.recurringMode == RecurringMode::ScheduledReadings ? L"--scheduled-readings-plan" : L"--next-readings-plan";
    if (!matches(options.archiveArg, L"--open-review") || !matches(options.extendArg, L"--extend-review") ||
        !matches(options.tariffArg, L"--tariff") || !matches(options.intervalsArg, L"--intervals") ||
        !matches(options.invoiceArg, L"--invoice") || !matches(options.ratesArg, L"--octopus-rates") ||
        !matches(options.nextCycleArg, L"--next-cycle-review") || !matches(options.rollForwardArg, L"--roll-forward") ||
        !matches(options.recurringModeArg, recurringFlag) ||
        !matches(options.prepareCycleArg, L"--prepare-next-cycle") || !matches(options.importPagesArg, L"--import-octopus-pages") ||
        !matches(options.previousCycleArg, L"--previous-cycle") || !matches(options.prepaidArg, L"--prepaid"))
        return std::nullopt;
    if (options.reviewCutoffArg &&
        (!matches(options.reviewCutoffArg, L"--as-of-utc-ms") ||
         std::wstring_view(args.values[options.reviewCutoffArg]) != std::to_wstring(options.reviewCutoffUtcMs)))
        return std::nullopt;
    if (options.watchArg && (!matches(options.watchArg, L"--watch-every-seconds") ||
        std::wstring_view(args.values[options.watchArg]) != std::to_wstring(options.watchSeconds)))
        return std::nullopt;
    if (options.allowPublicRatesArg && std::wstring_view(args.values[options.allowPublicRatesArg]) != L"--allow-public-rates")
        return std::nullopt;
    if (options.confirmImportArg &&
        std::wstring_view(args.values[options.confirmImportArg]) != L"--confirm-electricity-import")
        return std::nullopt;
    if (options.quietWatchArg && std::wstring_view(args.values[options.quietWatchArg]) != L"--watch-quiet")
        return std::nullopt;
    if (options.historyInvoicesArg &&
        std::wstring_view(args.values[options.historyInvoicesArg]) != L"--with-invoices")
        return std::nullopt;
    if (options.watchInvoicesArg && std::wstring_view(args.values[options.watchInvoicesArg]) != L"--watch-invoices")
        return std::nullopt;
    if (options.dnsServerArg && (!matches(options.dnsServerArg, L"--dns-server") ||
        std::wstring_view(args.values[options.dnsServerArg]) != std::wstring(options.dnsServer.begin(), options.dnsServer.end())))
        return std::nullopt;
    const auto matchesOutput = [&](int index, std::wstring_view flag) {
        return !index || (matches(index, flag) &&
               std::wstring_view(args.values[index]) == std::wstring(options.outputName.begin(), options.outputName.end()));
    };
    if (!matchesOutput(options.saveArg, L"--save-review") || !matchesOutput(options.saveTariffArg, L"--save-tariff") ||
        !matchesOutput(options.saveRegisterArg, L"--save-register") || !matchesOutput(options.saveReadingsArg, L"--save-readings"))
        return std::nullopt;
    const auto path = [&](int i) { return i ? std::filesystem::path(args.values[i]) : std::filesystem::path{}; };
#else
    const auto path = [&](int i) { return i ? std::filesystem::path(argv[i]) : std::filesystem::path{}; };
#endif
    return MeterBillPaths{path(options.tariffArg), path(options.intervalsArg), path(archiveIndex),
                          path(options.ratesArg), path(options.invoiceArg), path(options.prepaidArg), path(options.previousCycleArg),
                          path(options.importPagesArg)};
}

const char *decodeIntervals(const JsonValue &intervals, const BillOpening &opening, int64_t cutoff,
                            const char *lateReadingError, const DemandChargeRule *apparent, bool netPower,
                            std::vector<PowerInterval> &readings, std::vector<std::string> &apparentSources) {
    auto cursor = opening.throughUtcMs;
    readings.reserve(intervals.items.size());
    if (apparent) apparentSources.reserve(intervals.items.size());
    for (const auto &row : intervals.items) {
        PowerInterval reading;
        if (!row.isObject() || (!apparent && row.members.size() != 3) ||
            !instant(row.find("begin_utc_ms"), reading.beginUtcMs) ||
            !instant(row.find("end_utc_ms"), reading.endUtcMs) || reading.endUtcMs <= reading.beginUtcMs)
            return "invalid meter interval";
        if (netPower) {
            const auto *watts=row.find("average_net_watts");
            if (!watts) return electricityStatusName(ElectricityStatus::ExportEvidenceMissing);
            if (!signedAmount(watts,reading.watts)) return "invalid signed meter power";
            auto &exported=reading.exportWatts.emplace();
            if (reading.watts.negative()) {
                if (!exported.subtract(reading.watts,&exported)) return "meter power exceeds arithmetic capacity";
                reading.watts={};
            }
        } else if (!nonnegativeAmount(row.find("average_watts"),reading.watts))
            return "invalid meter interval";
        if (apparent) {
            const auto *va = row.find("average_va");
            const auto *pf = row.find("power_factor");
            if (!va && !pf) return electricityStatusName(ElectricityStatus::DemandApparentEvidence);
            if (row.members.size() != 4 || (va && pf)) return "invalid meter interval";
            if (va) {
                if (!nonnegativeAmount(va, reading.apparentVa.emplace()) ||
                    reading.apparentVa->compare(reading.watts) < 0)
                    return "apparent VA must be at least the active watts of the same meter interval";
                apparentSources.push_back("va " + va->text);
            } else {
                if (!apparent->powerFactorVaPlaces)
                    return "the saved tariff requires measured apparent VA";
                ExactDecimal factor;
                if (!nonnegativeAmount(pf, factor) ||
                    !apparentDemandFromPowerFactor(*apparent, reading.watts, factor, reading.apparentVa.emplace()))
                    return "invalid power factor or insufficient declared VA precision";
                apparentSources.push_back("pf " + pf->text);
            }
        }
        if (reading.beginUtcMs != cursor) return "interval coverage has a gap or overlap";
        if (reading.endUtcMs > cutoff) return lateReadingError;
        cursor = reading.endUtcMs;
        readings.push_back(std::move(reading));
    }
    return nullptr;
}

const char *decodeRegister(const JsonValue &rows, const BillOpening &opening, int64_t cutoff,
                           std::vector<MeterImportInterval> &readings, ExactDecimal &start, ExactDecimal &end,
                           ExactDecimal *secondaryStart, bool apparent) {
    std::optional<int64_t> previousUtc;
    ExactDecimal previous, previousSecondary;
    const auto *secondaryField=apparent?"apparent_kvah":"exported_kwh";
    readings.reserve(rows.items.size() - 1);
    for (const auto &row : rows.items) {
        int64_t utc = 0;
        ExactDecimal cumulative, secondary;
        if (!row.isObject() || row.members.size() != (secondaryStart ? 3u : 2u) ||
            !instant(row.find("utc_ms"), utc, true) || !nonnegativeAmount(row.find("imported_kwh"), cumulative) ||
            (secondaryStart && !nonnegativeAmount(row.find(secondaryField), secondary)))
            return "invalid meter register reading";
        if (utc > cutoff) return "meter register reading is after the review cutoff";
        if (!previousUtc) {
            if (utc != opening.throughUtcMs) return "first meter register reading must match the saved opening time";
            start = cumulative;
            if (secondaryStart) *secondaryStart = secondary;
        } else {
            if (utc <= *previousUtc) return "meter register timestamps must strictly increase";
            if (cumulative.compare(previous) < 0) return "meter register decreased (reset or rollover is unsupported)";
            ExactDecimal delta;
            if (!cumulative.subtract(previous, &delta)) return "meter register quantity exceeds arithmetic capacity";
            auto imported = electricityKwh(delta);
            if (!imported) return "meter register quantity exceeds arithmetic capacity";
            std::optional<ElectricityAmount> secondaryDelta;
            if (secondaryStart) {
                if (!secondary.subtract(previousSecondary, &delta))
                    return "paired register quantity exceeds arithmetic capacity";
                if (delta.negative()) return apparent ? "apparent register decreased (reset or rollover is unsupported)"
                                                      : "export register decreased (reset or rollover is unsupported)";
                secondaryDelta = electricityKwh(delta);
                if (!secondaryDelta) return "paired register quantity exceeds arithmetic capacity";
                if (apparent && secondaryDelta->numerator.compare(imported->numerator) < 0)
                    return "apparent register delta must be at least the active import delta";
            }
            MeterImportInterval interval{*previousUtc,utc,std::move(*imported)};
            (apparent ? interval.apparentKvah : interval.exportedKwh)=std::move(secondaryDelta);
            readings.push_back(std::move(interval));
        }
        previous = std::move(cumulative);
        previousSecondary = std::move(secondary);
        previousUtc = utc;
    }
    end = std::move(previous);
    return nullptr;
}

const char *decodeMeterReadings(const std::string &text, const TariffDeclaration &declaration, int64_t cutoff,
                               const char *lateReadingError, MeterReadings &readings,
                               const std::function<bool()> &stopped) {
    const auto &opening = declaration.opening;
    const bool annual = declaration.resetSchedule && declaration.resetSchedule->annualMonth;
    const bool exported = declaration.plan.exportPayment.has_value();
    const bool extended = annual || declaration.plan.hasDemand() || exported;
    const auto byteLimit = extended ? maxMeterFileBytes : maxOrdinaryMeterFileBytes;
    const auto intervalLimit = extended ? maxAnnualMeterIntervals : maxOrdinaryMeterIntervals;
    if (text.size() > byteLimit) return "interval file unavailable, oversized or not a regular file";
    JsonValue root;
    if (!parseJsonStrict(text, &root, nullptr, {byteLimit, 5}) || !root.isObject())
        return "invalid interval document";
    const auto *schema = root.find("schema_version");
    const auto *scope = root.find("scope");
    uint64_t version = 0;
    if (!schema || !schema->asUint64(&version) || version < 1 || version > 7 || !scope || !scope->isString() ||
        root.members.size() != (version == 3 ? 5u : 3u))
        return "invalid interval document";
    const auto *apparent = declaration.plan.apparentDemandRule();
    const bool apparentInput = version == 4 || version == 5;
    if (apparent && !apparentInput) return electricityStatusName(ElectricityStatus::DemandApparentEvidence);
    if (!apparent && apparentInput) return "apparent meter readings require a saved kVA tariff";
    if (exported && version < 6) return electricityStatusName(ElectricityStatus::ExportEvidenceMissing);
    if (!exported && version >= 6) return "export readings require a saved export-payment tariff";
    const bool isPower = version == 1 || version == 4 || version == 6;
    const bool isRegister = version == 2 || version == 5 || version == 7;
    const auto *rows = root.find(isPower ? "intervals" : isRegister ? "register_readings" : "consumption");
    if (!rows || !rows->isArray()) return "invalid interval document";
    const auto size = rows->items.size();
    if (isRegister ? size < 2 || size > intervalLimit + 1 : size < 1 || size > intervalLimit)
        return "invalid interval document";
    if (scope->text != opening.evidence.scope) return "meter scope differs from the saved opening";
    readings.version = version;
    if (isPower)
        return decodeIntervals(*rows, opening, cutoff, lateReadingError,
                               apparent, exported,
                               readings.series.emplace<std::vector<PowerInterval>>(), readings.apparentSources);
    auto &imports = readings.series.emplace<std::vector<MeterImportInterval>>();
    if (isRegister)
        return decodeRegister(*rows, opening, cutoff, imports, readings.registerStart.emplace(),
                              readings.registerEnd.emplace(), apparent || exported ? &readings.secondaryRegisterStart.emplace() : nullptr, apparent);

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
    ElectricityAmount singleDemandSubtotal,billablePeak,ratchetFloor;
    std::string namedDemandExact;
    const auto appendDemand = [&](const DemandChargeRule &demand, const ElectricityAmount &measured,
                                  const std::vector<ElectricityAmount> &history, ElectricityAmount &subtotal) {
        std::string peak, amount;
        if (!billableDemand(demand,measured,history,billablePeak,ratchetFloor) ||
            !measured.format(6,&peak) || !demand.charge(billablePeak,&subtotal) ||
            !subtotal.format(6,&amount)) return false;
        const std::string unit = demand.unit();
        report += "  Peak demand " + peak + " " + unit + " over " + std::to_string(demand.windowMinutes) + " minute windows\n";
        if (!demand.eligiblePeriods.empty())
            report += "  Demand uses only windows in the declared eligibility periods\n";
        if (demand.ratchet) {
            std::string floor,billable;
            if (!ratchetFloor.format(6,&floor) || !billablePeak.format(6,&billable)) return false;
            report += "  Demand ratchet floor " + floor + " " + unit + "\n";
            report += "  Billable demand " + billable + " " + unit + "\n";
        }
        report += "  Demand charge before subtotal multiplier " + amount + " " + currency + "\n";
        return true;
    };
    const auto &ending = *accrual.ending;
    if (const auto &demand=declaration.plan.demandCharge) {
        if (!appendDemand(*demand,*ending.peakDemand,ending.demandHistory,singleDemandSubtotal)) return false;
    }
    const auto &named = declaration.plan.demandCharges;
    if (!named.empty()) {
        namedDemandExact = "[";
        for (size_t i = 0; i < named.size(); ++i) {
            const auto &item = named[i];
            const auto &state = ending.demandOpenings[i];
            ElectricityAmount amount;
            report += "  Demand charge " + std::to_string(i + 1) + " (declaration order)\n";
            if (!appendDemand(item.rule,state.peak,state.history,amount)) return false;
            if (i) namedDemandExact += ',';
            namedDemandExact += "[" + jsonQuote(item.key) + "," + jsonQuote(item.rule.unit()) +
                "," + jsonQuote(state.peak.numerator.text()) + "," + jsonQuote(billablePeak.numerator.text()) +
                "," + jsonQuote(ratchetFloor.numerator.text()) + "," + jsonQuote(amount.numerator.text()) + "]";
        }
        namedDemandExact += ']';
        ElectricityAmount subtotal;
        std::string amount;
        if (!demandSubtotal(declaration.plan,ending,subtotal) || !subtotal.format(6,&amount)) return false;
        report += "  Combined demand charge before subtotal multiplier " + amount + " " + currency + "\n";
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
    std::string taxDigest;
    if (const auto &tax = accrual.tax) {
        std::string subtotal, minimum, added;
        if (!accrual.ending->accruedSubtotal.format(6, &subtotal) ||
            !tax->minimumAdjustment.format(6, &minimum) || !tax->added.format(6, &added)) return false;
        report += "  Declared supply subtotal before added taxes " + subtotal + " " + currency + "\n";
        if (!tax->minimumAdjustment.numerator.zero())
            report += "  Minimum-charge adjustment " + minimum + " " + currency + "\n";
        report += "  Ordered taxes through " + std::to_string(accrual.ending->throughUtcMs) + " UTC ms\n";
        std::string record;
        const auto appendBases = [&](const BillTaxOpening &bases) {
            for (const auto &part : bases.components) record += part.numerator.text() + '\n';
            record += bases.billedKwh.numerator.text() + '\n';
        };
        if (const auto &segments = accrual.ending->taxSegments) {
            record += "tax-by-service-and-cycle-close\n";
            for (const auto &segment : *segments) {
                record += "segment " + std::to_string(segment.beginUtcMs) + '\n' +
                    segment.importedKwh.numerator.text() + '\n';
                appendBases(segment.bases);
            }
            if (!accrual.settledTotal)
                report += "  Demand and minimum adjustments use the closing tax period.\n";
        } else appendBases(*accrual.ending->tax);
        record += tax->minimumAdjustment.numerator.text() + '\n';
        std::optional<int64_t> previousPeriod;
        for (const auto &line : tax->lines) {
            if (line.periodBeginUtcMs && line.periodBeginUtcMs != previousPeriod) {
                const auto begin = std::to_string(*line.periodBeginUtcMs);
                report += "  Tax period from " + begin + " UTC ms\n";
                record += "period " + begin + '\n';
                previousPeriod = line.periodBeginUtcMs;
            }
            std::string base, charge;
            if (!line.base.format(6, &base) || !line.added.format(6, &charge)) return false;
            report += "  Tax " + line.key + " added " + charge + " " + currency;
            if (!line.base.numerator.zero()) {
                report += " on " + base + (line.kind == TaxKind::Fraction ? " " + currency : " kWh");
                if (line.periodBeginUtcMs && line.kind != TaxKind::Fraction)
                    report += line.kind == TaxKind::BilledKwh ? " billed" : " measured";
            }
            bool first = true;
            for (unsigned i = 0; i < taxComponentCount; ++i) {
                if (!(line.included & (1u << i))) continue;
                report += first ? " | included in " : ", ";
                report += taxComponentNames[i];
                first = false;
            }
            report += '\n';
            if (line.periodBeginUtcMs)
                record += "kind " + std::to_string(static_cast<unsigned>(line.kind)) + '\n';
            record += line.key + '\n' + std::to_string(line.included) + '\n' +
                line.base.numerator.text() + '\n' + line.added.numerator.text() + '\n';
        }
        report += "  Added supply taxes to date " + added + " " + currency + "\n";
        taxDigest = sha256Hex(record);
    }
    if (const auto &exported=accrual.exportAccrual) {
        std::string quantity, payment, cyclePayment, netChange, netTotal;
        if (!exported->newKwh.format(6,&quantity) || !exported->paymentChange.format(6,&payment) ||
            !exported->paymentTotal.format(6,&cyclePayment) || !exported->netChange.format(6,&netChange) ||
            !exported->netTotal.format(6,&netTotal)) return false;
        report += "  Exported usage " + quantity + " kWh\n";
        report += "  Export payment from new readings " + payment + " " + currency + "\n";
        report += "  Export payment to date " + cyclePayment + " " + currency + "\n";
        report += "  Net change after export payment " + netChange + " " + currency + "\n";
        report += std::string(accrual.settledTotal ? "  Net electricity cost at closing " : "  Provisional net electricity cost ") +
                  netTotal + " " + currency + "\n";
    }
    if (const auto &credit=accrual.restrictedCredit) {
        std::string eligible, applied, expired, remaining;
        if (!credit->eligible.format(6,&eligible) || !credit->applied.format(6,&applied) ||
            !credit->expired.format(6,&expired) || !credit->remaining.format(6,&remaining)) return false;
        report += "  Eligible import energy charges to date " + eligible + " " + currency + "\n";
        report += "  Restricted credit applied " + applied + " " + currency + "\n";
        report += "  Restricted credit expired " + expired + " " + currency + "\n";
        report += "  Restricted credit remaining " + remaining + " " + currency + "\n";
        if (credit->cashDue) {
            std::string cash;
            if (!credit->cashDue->format(6,&cash)) return false;
            report += "  Cash due after restricted credit " + cash + " " + currency + "\n";
        } else report += "  Eligible charges are provisional. Restricted credit is applied only at closing.\n";
    }
    exact += "[" + std::to_string(opening.cycleBeginUtcMs) + "," + std::to_string(opening.cycleEndUtcMs) +
              "," + std::to_string(opening.throughUtcMs) + "," + std::to_string(ending.throughUtcMs) +
              "," + jsonQuote(ending.timeDatabaseVersion) + "," + jsonQuote(usage.numerator.text()) +
              "," + jsonQuote(ending.importedKwh.numerator.text()) + "," + jsonQuote(ending.accruedSubtotal.numerator.text()) +
              "," + jsonQuote(accrual.provisionalChange->numerator.text()) + "," + jsonQuote(accrual.provisionalTotal->numerator.text()) +
              "," + (accrual.settledTotal ? jsonQuote(accrual.settledTotal->numerator.text()) : "null") +
              (accrual.settlementDependent ? ",true" : ",false");
    if (accrual.billedKwh) exact += "," + jsonQuote(accrual.billedKwh->numerator.text());
    if (seasonal) exact += "," + jsonQuote(seasonal->allowanceKwh.text());
    if (ending.peakDemand)
        exact += "," + jsonQuote(ending.peakDemand->numerator.text()) + "," + jsonQuote(singleDemandSubtotal.numerator.text());
    if (declaration.plan.demandCharge && declaration.plan.demandCharge->ratchet)
        exact += "," + jsonQuote(ratchetFloor.numerator.text()) + "," + jsonQuote(billablePeak.numerator.text());
    if (ending.apparentDemand) exact += ",\"kVA\"";
    if (!namedDemandExact.empty()) exact += ",\"demand_charges\"," + namedDemandExact;
    if (!taxDigest.empty()) exact += ",\"ordered_tax_digest\"," + jsonQuote(taxDigest);
    if (const auto &exported=accrual.exportAccrual)
        exact += ",\"export_payment\"," + jsonQuote(exported->newKwh.numerator.text()) +
                 "," + jsonQuote(ending.exports->exportedKwh.numerator.text()) +
                 "," + jsonQuote(exported->paymentChange.numerator.text()) +
                 "," + jsonQuote(exported->paymentTotal.numerator.text()) +
                 "," + jsonQuote(exported->netChange.numerator.text()) +
                 "," + jsonQuote(exported->netTotal.numerator.text());
    if (const auto &credit=accrual.restrictedCredit) {
        const auto &state=*ending.restrictedCredit;
        exact += ",\"restricted_credit\"," + jsonQuote(state.accruedImportSubtotal.numerator.text()) +
                 "," + jsonQuote(credit->eligible.numerator.text()) +
                 "," + jsonQuote(credit->applied.numerator.text()) +
                 "," + jsonQuote(credit->expired.numerator.text()) +
                 "," + jsonQuote(credit->remaining.numerator.text()) +
                 "," + (credit->cashDue ? jsonQuote(credit->cashDue->numerator.text()) : "null") + ",[";
        bool first=true;
        for (const auto &lot:state.lots) {
            if (!first) exact += ',';
            first=false;
            exact += "[" + std::to_string(lot.expiresUtcMs) + "," + jsonQuote(lot.remaining.numerator.text()) + "]";
        }
        exact += ']';
    }
    exact += "]\n";
    return true;
}

struct CompletedSupplyCycle {
    InvoicePeriod period;
    ElectricityAmount charge;
};

struct InvoiceReviewDetails {
    std::string report;
    std::optional<SupplyInvoiceComparison> supply;
};

template<class Interval>
const char *reviewReadings(const std::vector<Interval> &readings, const TariffDeclaration &declaration, int64_t cutoff,
                           const std::function<bool()> &stopped, std::string &report, std::string &exactResults,
                           const MeterInvoice *invoice, std::string *exactInvoice,
                           std::string *meterOnlyExact = nullptr,
                           std::vector<CompletedSupplyCycle> *completedSupply = nullptr,
                           InvoiceReviewDetails *invoiceDetails = nullptr) {
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
                slice.intervals.push_back({cursor, end, reading.watts, reading.apparentVa, reading.exportWatts});
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
        if (completedSupply && accrual.settledTotal && opening.throughUtcMs == opening.cycleBeginUtcMs)
            completedSupply->push_back({{opening.cycleBeginUtcMs, opening.cycleEndUtcMs}, *accrual.settledTotal});
        if (invoice) {
            const auto start = report.size();
            if (const auto *problem = appendInvoiceComparison(*invoice, cycle - 1, opening, accrual,
                                                              declaration.plan.currency, report, exactInvoice,
                                                              invoiceDetails ? &invoiceDetails->supply : nullptr))
                return problem;
            if (invoiceDetails) invoiceDetails->report.append(report, start);
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

template<class Interval>
const char *reviewExportStatement(const std::vector<Interval> &readings, const TariffDeclaration &declaration,
                                 const InvoicePeriod &period, int64_t cutoff, const std::function<bool()> &stopped,
                                 ExportPeriodEstimate &estimate) {
    constexpr bool isRegister = std::is_same_v<Interval, MeterImportInterval>;
    using Trace = std::conditional_t<isRegister, MeterImportTrace, PowerTrace>;
    if (period.begin < readings.front().beginUtcMs || period.end > readings.back().endUtcMs)
        return "export statement period lies outside the reviewed meter readings";
    auto reading = std::upper_bound(readings.begin(), readings.end(), period.begin,
        [](int64_t at, const Interval &interval) { return at < interval.endUtcMs; });
    Trace trace{declaration.opening.evidence, {}};
    for (auto cursor = period.begin; cursor < period.end && reading != readings.end(); ++reading) {
        if (stopped()) return "review cancelled";
        const auto begin = (std::max)(cursor, reading->beginUtcMs);
        const auto end = (std::min)(reading->endUtcMs, period.end);
        if (end <= begin) break;
        if constexpr (isRegister) {
            if (!reading->exportedKwh) return electricityStatusName(ElectricityStatus::ExportEvidenceMissing);
            if ((begin != reading->beginUtcMs || end != reading->endUtcMs) && !reading->exportedKwh->numerator.zero())
                return "export register needs a reading at a statement boundary";
            trace.intervals.push_back({begin, end, {}, {}, reading->exportedKwh});
        } else {
            trace.intervals.push_back({begin, end, reading->watts, {}, reading->exportWatts});
        }
        cursor = end;
    }
    estimate = estimateExportPeriod(*declaration.plan.exportPayment, trace, period.begin, period.end, cutoff, stopped);
    return estimate.status == ElectricityStatus::Ok ? nullptr : electricityStatusName(estimate.status);
}

template<class Interval>
const char *reviewIndependentInvoice(const IndependentInvoice &invoice, const std::vector<CompletedSupplyCycle> &completed,
                                     const std::vector<Interval> &readings, const TariffDeclaration &declaration,
                                     int64_t cutoff, const std::function<bool()> &stopped,
                                     std::string &report, std::string *exact,
                                     std::optional<SupplyInvoiceComparison> *supplyComparison) {
    const auto &currency = declaration.plan.currency;
    const auto netPeriod = commonStatementPeriod(invoice);
    InvoiceTotals supply, exported;
    report += "Independent supply and export statements\n";
    for (const auto &statement : invoice.supply.periods) {
        if (stopped()) return "review cancelled";
        const auto found = std::find_if(completed.begin(), completed.end(), [&](const auto &cycle) {
            return cycle.period.begin == statement.period.begin && cycle.period.end == statement.period.end;
        });
        if (found == completed.end()) return "supply statement must match a complete reviewed billing cycle from a zero opening";
        if (!appendStatement("supply charge", "supply_statement", invoice.supply.precision, statement, found->charge,
                             currency, (netPeriod || supplyComparison) ? &supply : nullptr, report, exact))
            return "invoice comparison amount exceeds arithmetic capacity";
    }
    for (const auto &statement : invoice.exported.periods) {
        ExportPeriodEstimate estimate;
        if (const auto *problem = reviewExportStatement(readings, declaration, statement.period, cutoff, stopped, estimate))
            return problem;
        std::string quantity;
        if (!estimate.exportedKwh.format(6, &quantity) ||
            !appendStatement("export payment", "export_statement", invoice.exported.precision, statement, estimate.payment,
                             currency, netPeriod ? &exported : nullptr, report, exact))
            return "invoice comparison amount exceeds arithmetic capacity";
        report += "  Exported usage for statement " + quantity + " kWh\n";
        if (exact) *exact += "[\"statement_exported_kwh\"," + jsonQuote(estimate.exportedKwh.numerator.text()) + "]\n";
    }
    const auto places = (std::max)(invoice.supply.precision.places, invoice.exported.precision.places);
    if (!appendStatementNet(netPeriod, places, supply, exported, currency, report, exact))
        return "invoice comparison amount exceeds arithmetic capacity";
    if (supplyComparison && !invoice.supply.periods.empty())
        *supplyComparison = SupplyInvoiceComparison{supply, invoice.supply.precision.places};
    return nullptr;
}

struct MeterReview {
    TariffDeclaration declaration;
    MeterReadings readings;
    std::optional<PrepaidReviewInput> prepaid;
    std::optional<OctopusRateImport> supplier;
    std::string report, exact;
};

const char *decodeMeterTariff(const std::string &bytes, TariffDeclaration &declaration, bool &namedTariff) {
    JsonValue root;
    if (!parseJsonStrict(bytes, &root, nullptr))
        return "saved tariff declaration unavailable or invalid";
    const auto *schema = root.find("schema_version");
    uint64_t schemaVersion = 0;
    const auto *plan=root.find("plan"), *policy=root.find("reset_policy");
    namedTariff = schema && schema->asUint64(&schemaVersion) &&
        (schemaVersion == 4 || (schemaVersion >= 5 && schemaVersion <= tariffDeclarationLatestVersion &&
         (usesNamedClock(plan ? plan->find("clock") : nullptr) || usesNamedClock(policy ? policy->find("clock") : nullptr))));
    if (namedTariff && configureBundledCivilTimeData() != CivilStatus::Ok)
        return "reviewed tariff time database unavailable or invalid";
    if (!decodeTariffDeclaration(root, &declaration)) return "saved tariff declaration unavailable or invalid";
    return nullptr;
}

const char *prepareMeterReview(const ReviewOriginal &original, int64_t now, bool showCutoff,
                               const char *lateReadingError, const std::function<bool()> &stopped,
                               MeterReview &review) {
    auto &declaration = review.declaration;
    if (stopped()) return "review cancelled";
    bool namedTariff = false;
    if (const auto *problem = decodeMeterTariff(original.tariff, declaration, namedTariff)) return problem;
    if (now < 1 || now > meterBillMaxUtcMs || static_cast<int64_t>(declaration.confirmedUtc * 1000) > now)
        return "local clock precedes the review or is unavailable";
    const auto cutoff = original.cutoff;
    if (cutoff > now) return "review cutoff is in the future";
    if (declaration.plan.hasDemandHistory() &&
        declaration.opening.cycleBeginUtcMs > cutoff)
        return "completed demand history is after the review cutoff";
    if (stopped()) return "review cancelled";
    if (const auto *problem = decodeMeterReadings(original.intervals, declaration, cutoff, lateReadingError,
                                                   review.readings, stopped))
        return problem;

    if (!original.prepaid.empty()) {
        if (!review.readings.registerStart) return "prepaid review requires cumulative meter register readings";
        const auto &series=std::get<std::vector<MeterImportInterval>>(review.readings.series);
        if (const auto *problem=decodePrepaidReview(original.prepaid,declaration,cutoff,series.back().endUtcMs,
                                                   review.prepaid.emplace()))
            return problem;
    }
    auto &report = review.report;
    report = std::holds_alternative<std::vector<MeterImportInterval>>(review.readings.series)
        ? "Estimated electricity costs from a declared tariff and user-supplied meter register readings\n"
          "Uses exact differences between cumulative import readings in kWh.\n"
        : "Estimated electricity costs from a declared tariff and user-supplied meter intervals\n"
          "Average import is applied uniformly within each interval.\n";
    if (review.readings.version >= 6)
        report += "Supply charges use imported energy. Declared export payment is shown separately.\n";
    if (declaration.plan.exportPayment && !declaration.plan.exportPayment->dynamic.empty())
        report += "Export uses the declared UTC rate intervals. A negative payment increases electricity cost.\n";
    if (review.readings.version == 5)
        report += "Paired kVAh differences establish apparent demand. Active kWh supplies energy pricing.\n";
    else if (review.readings.version == 4)
        report += "Each interval supplies measured VA or the declared power-factor conversion.\n";
    if (review.readings.octopusObserved) {
        report = "Estimated electricity costs from declared Octopus Energy whole-meter electricity imports\n";
        report += "API quantities are reported to 0.001 kWh. Local bundle observation " +
            std::to_string(review.readings.octopusObserved) + " UTC ms.\n";
    }
    if (namedTariff) report += std::string("Reviewed IANA time database ") + tariffTzdbVersion + "\n";
    if (declaration.plan.restrictedCreditAtClose)
        report += "Restricted credits apply only to import energy at cycle close and expire at their stated UTC instants.\n";
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
        auto &imported = review.supplier.emplace();
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

template<class Interval>
const char *addPrepaidReview(const std::optional<PrepaidReviewInput> &prepaid,const std::vector<Interval> &series,int64_t cutoff,
                             const std::function<bool()> &stopped,std::string &report,std::string &exact) {
    if (!prepaid) return nullptr;
    if constexpr (std::is_same_v<Interval,MeterImportInterval>)
        return reviewPrepaid(*prepaid,series,cutoff,stopped,report,exact);
    else
        return "prepaid review requires cumulative meter register readings";
}

const char *calculateMeterReview(const ReviewOriginal &original, bool includeInvoiceInExact,
                                 const std::function<bool()> &stopped, MeterReview &review,
                                 std::string *meterOnlyExact = nullptr,
                                 std::vector<CompletedSupplyCycle> *supplyPeriods = nullptr,
                                 InvoiceReviewDetails *invoiceDetails = nullptr) {
    std::optional<MeterInvoice> invoice;
    if (!original.invoice.empty()) {
        if (const auto *problem = decodeMeterInvoice(original.invoice, review.declaration, invoice.emplace()))
            return problem;
        if (!invoice->independent) {
            const auto start = review.report.size();
            review.report += invoice->kind == MeterInvoiceKind::Annual ? "Annual reconciliation uses " :
                             invoice->kind == MeterInvoiceKind::Export ? "Export invoice reconciliation uses " :
                                                                        "Invoice comparison uses ";
            review.report += std::to_string(invoice->places) + " decimal places with ties ";
            review.report += invoice->ties == DecimalTies::ToEven ? "rounded to even.\n" : "rounded away from zero.\n";
            if (invoiceDetails) invoiceDetails->report += "  " + review.report.substr(start);
        }
    }
    return std::visit([&](const auto &series) {
        const auto *independent = invoice && invoice->independent ? &*invoice->independent : nullptr;
        std::vector<CompletedSupplyCycle> invoicePeriods;
        auto *completed = supplyPeriods ? supplyPeriods : independent ? &invoicePeriods : nullptr;
        auto *exactInvoice = includeInvoiceInExact ? &review.exact : nullptr;
        const auto *problem=reviewReadings(series, review.declaration, original.cutoff, stopped, review.report, review.exact,
                                           invoice && !independent ? &*invoice : nullptr, exactInvoice, meterOnlyExact,
                                           completed, invoiceDetails);
        if (problem) return problem;
        if (independent) {
            const auto start = review.report.size();
            if (const auto *invoiceProblem = reviewIndependentInvoice(*independent, *completed, series, review.declaration,
                    original.cutoff, stopped, review.report, exactInvoice,
                    invoiceDetails ? &invoiceDetails->supply : nullptr)) return invoiceProblem;
            if (invoiceDetails) invoiceDetails->report.append(review.report, start);
        }
        if (!review.prepaid) return static_cast<const char *>(nullptr);
        const auto start=review.exact.size();
        if (const auto *prepaidProblem=addPrepaidReview(review.prepaid,series,original.cutoff,stopped,review.report,review.exact))
            return prepaidProblem;
        if (meterOnlyExact) meterOnlyExact->append(review.exact,start);
        return static_cast<const char *>(nullptr);
    }, review.readings.series);
}

// Bind the previous meter observations as well as their exact accrued amounts. Absolute register
// readings are determined by the first reading and the exact adjacent differences.
template<class Interval>
std::string meterPrefixDigest(const MeterReadings &readings, const std::vector<Interval> &series,
                              const std::string &meterExact) {
    std::string original = std::to_string(readings.version) + '\n';
    if (readings.registerStart) original += readings.registerStart->text() + '\n';
    if (readings.secondaryRegisterStart) original += readings.secondaryRegisterStart->text() + '\n';
    size_t index = 0;
    for (const auto &interval : series) {
        original += std::to_string(interval.beginUtcMs) + ' ' + std::to_string(interval.endUtcMs) + ' ';
        if constexpr (std::is_same_v<Interval, MeterImportInterval>) {
            original += interval.importedKwh.numerator.text();
            if (interval.apparentKvah) original += ' ' + interval.apparentKvah->numerator.text();
            if (interval.exportedKwh) original += ' ' + interval.exportedKwh->numerator.text();
        } else {
            original += interval.watts.text();
            if (interval.apparentVa) original += ' ' + readings.apparentSources[index];
            if (interval.exportWatts) original += ' ' + interval.exportWatts->text();
        }
        ++index;
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
        if (const auto *problem=addPrepaidReview(review.prepaid,prefix,previous.cutoff,stopped,ignored,prefixExact)) return problem;
        if (meterPrefixDigest(review.readings, prefix, prefixExact) != previous.prefixSha256)
            return "new readings or exact charges differ from the saved review prefix";
        if (original.cutoff != previous.cutoff) {
            ignored.clear();
            std::string currentExact;
            if (const auto *problem = reviewReadings(prefix, review.declaration, original.cutoff, stopped, ignored,
                                                     currentExact, nullptr, nullptr)) return problem;
            if (const auto *problem=addPrepaidReview(review.prepaid,prefix,original.cutoff,stopped,ignored,currentExact)) return problem;
            if (currentExact != prefixExact) return "the new cutoff would change the saved review charges";
        }
        return nullptr;
    }, review.readings.series);
}

const char *cycleClockToken(CivilClockKind kind) {
    switch (kind) {
        case CivilClockKind::Utc: return "utc";
        case CivilClockKind::FixedStandardOffset: return "fixed_standard";
        case CivilClockKind::NamedIana: return "named_iana";
    }
    return nullptr;
}

// The plan key and public price window can advance. These fields identify the continuing provider,
// meter alias, input representation, time database and reset rule.
std::optional<std::string> completeOctopusCycleBinding(const MeterReview &review, const std::string &paymentMethod) {
    const auto &declaration = review.declaration;
    const auto &plan = declaration.plan;
    const auto &opening = declaration.opening;
    const auto &meter = opening.evidence;
    const auto *series = std::get_if<std::vector<MeterImportInterval>>(&review.readings.series);
    if (!declaration.resetSchedule || review.prepaid || plan.exportPayment ||
        plan.restrictedCreditAtClose ||
        (review.readings.version != 2 && review.readings.version != 3) ||
        !series || series->empty() || opening.throughUtcMs != opening.cycleBeginUtcMs ||
        !opening.importedKwh.numerator.zero() || !opening.accruedSubtotal.numerator.zero() ||
        series->front().beginUtcMs != opening.cycleBeginUtcMs || series->back().endUtcMs != opening.cycleEndUtcMs ||
        plan.revisions.empty() || plan.revisions.front().beginUtcMs != opening.cycleBeginUtcMs ||
        plan.revisions.back().endUtcMs != opening.cycleEndUtcMs)
        return std::nullopt;

    const auto &reset = *declaration.resetSchedule;
    const auto *rateClock = cycleClockToken(plan.clock.kind);
    const auto *resetClock = cycleClockToken(reset.clock.kind);
    if (!rateClock || !resetClock) return std::nullopt;
    // Stable wire tokens for values already validated by the declaration and rate reader.
    std::string binding = "canary-octopus-cycle-1\nwhole_meter_import\ndeclared_forecast\n";
    binding += meterHalfHourRounding;
    binding += '\n';
    for (const auto *field : {&declaration.country, &plan.territory, &plan.supplier, &plan.product,
                             &plan.currency, &paymentMethod, &meter.scope, &meter.adapter,
                             &opening.timeDatabaseVersion})
        binding += jsonQuote(*field) + '\n';
    const auto appendClock = [&](const CivilClock &clock, const char *token) {
        binding += std::string(token) + '\n' + jsonQuote(clock.zone) + '\n' +
                   std::to_string(clock.fixedOffsetSeconds) + '\n';
    };
    appendClock(plan.clock, rateClock);
    appendClock(reset.clock, resetClock);
    binding += std::to_string(review.readings.version) + '\n' +
               std::to_string(reset.day) + '\n' + std::to_string(reset.annualMonth) + '\n' +
               std::to_string(reset.bimonthlyStartMonth) + '\n';
    switch (reset.dailyFeeAllocation) {
        case DailyFeeAllocation::WholeDay: binding += "whole_day\n"; break;
        case DailyFeeAllocation::ProrateByCycleMinutes: binding += "prorate_by_cycle_minutes\n"; break;
        default: return std::nullopt;
    }
    return sha256Hex(binding);
}

std::optional<PreviousMeterCycle> bindPreviousCycle(const ReviewOriginal &original, const MeterReview &review,
                                                   std::string archiveSha256) {
    if (!review.supplier) return std::nullopt;
    auto binding = completeOctopusCycleBinding(review, review.supplier->paymentMethod);
    if (!binding) return std::nullopt;
    const auto &end = review.readings.registerEnd;
    std::string registerEnd = end ? end->text() : "-";
    if (registerEnd.size() > maxCycleRegisterBytes) return std::nullopt;
    return PreviousMeterCycle{original.cutoff, review.declaration.opening.cycleEndUtcMs, std::move(archiveSha256),
                              std::move(*binding), std::move(registerEnd)};
}

const char *verifyCycleLink(const ReviewOriginal &original, const MeterReview &review,
                            const std::string *expectedPayment = nullptr) {
    if (!original.previousCycle) return nullptr;
    const auto &previous = *original.previousCycle;
    const auto *payment = review.supplier ? &review.supplier->paymentMethod : expectedPayment;
    if (!payment) return "next-cycle review requires a matching Octopus rate capture";
    const auto binding = completeOctopusCycleBinding(review, *payment);
    if (!binding) return "next-cycle review requires one complete recurring Octopus cycle and matching rate window";
    if (previous.closedAt != review.declaration.opening.cycleBeginUtcMs)
        return "the new cycle does not start at the previous closing time";
    if (*binding != previous.bindingSha256)
        return "the Octopus tariff, payment method, meter or reset rule differs from the previous cycle";
    const auto &start = review.readings.registerStart;
    if ((start ? start->text() : "-") != previous.registerEnd)
        return "the new opening register differs from the previous closing register";
    return nullptr;
}

const char *replaySavedReview(const ReviewOriginal &original, int64_t now, const char *lateReadingError,
                             const std::function<bool()> &stopped, MeterReview &review,
                             std::string *meterOnlyExact = nullptr,
                             std::vector<CompletedSupplyCycle> *supplyPeriods = nullptr,
                             InvoiceReviewDetails *invoiceDetails = nullptr) {
    if (const auto *problem = prepareMeterReview(original, now, false, lateReadingError, stopped, review)) return problem;
    if (const auto *problem = verifyMeterPrefix(original, review, stopped)) return problem;
    if (const auto *problem = verifyCycleLink(original, review)) return problem;
    if (const auto *problem = calculateMeterReview(original, true, stopped, review, meterOnlyExact, supplyPeriods,
                                                   invoiceDetails)) return problem;
    return original.results == review.exact ? nullptr : "saved review amounts differ from the recalculated amounts";
}

bool sameClosingRecord(const PreviousMeterCycle &a, const PreviousMeterCycle &b) {
    return a.closedAt == b.closedAt && a.cutoff == b.cutoff && a.archiveSha256 == b.archiveSha256 &&
        a.bindingSha256 == b.bindingSha256 && a.registerEnd == b.registerEnd;
}

const char *verifyPreviousCycleArchive(const std::filesystem::path &path, const ReviewOriginal &original,
                                       int64_t now, const char *lateReadingError,
                                       const std::function<bool()> &stopped) {
    if (!original.previousCycle) return "saved review has no previous cycle link";
    const auto &link = *original.previousCycle;
    std::string bytes;
    ReviewOriginal predecessor;
    if (!readLocalReviewInput(path, maxArchiveBytes, bytes) || !decodeReviewOriginal(bytes, predecessor))
        return "previous cycle archive unavailable or invalid";
    const auto hash = sha256Hex(bytes);
    if (hash != link.archiveSha256) return "previous cycle archive checksum differs from the saved link";
    MeterReview earlier;
    if (const auto *problem = replaySavedReview(predecessor, now, lateReadingError, stopped, earlier)) return problem;
    const auto derived = bindPreviousCycle(predecessor, earlier, hash);
    if (!derived || !sameClosingRecord(*derived, link))
        return "previous cycle archive does not match the saved closing record";
    return nullptr;
}

struct PreparedOctopusCycle {
    TariffCycleBounds bounds;
    std::string bytes;
    bool publicCapture = false;
    bool olderTaxAnnotations = false;
};

JsonValue &nextTariffField(JsonValue &object, std::string_view name) {
    for (auto &member : object.members)
        if (member.first == name) return member.second;
    throw std::logic_error("validated tariff field missing");
}

const char *nextOctopusCycleBounds(const ReviewOriginal &original, int64_t closing, JsonValue &root,
                                   TariffCycleBounds &bounds) {
    if (!parseJsonStrict(original.tariff, &root, nullptr)) return "saved tariff declaration unavailable or invalid";
    JsonValue through;
    through.type = JsonValue::Type::Number;
    through.text = std::to_string(closing);
    const auto *reset = root.find("reset_policy");
    if (!reset || !deriveTariffRecurringCycle(*reset, through, bounds) ||
        bounds.beginUtcMs != closing || bounds.endUtcMs <= closing)
        return "next billing window unavailable under the saved reset rule";
    return nullptr;
}

const char *prepareNextOctopusCycle(const ReviewOriginal &original, const MeterReview &earlier,
                                    const PreviousMeterCycle &link, bool registerOnly, PreparedOctopusCycle &draft) {
    JsonValue root;
    if (const auto *problem = nextOctopusCycleBounds(original, link.closedAt, root, draft.bounds))
        return problem;
    const auto begin = std::to_string(link.closedAt);
    const auto &clock = earlier.declaration.plan.clock;
    const auto londonStart = civilAt(clock, draft.bounds.beginUtcMs);
    const auto londonEnd = civilAt(clock, draft.bounds.endUtcMs);
    if (londonStart.status != CivilStatus::Ok || londonEnd.status != CivilStatus::Ok ||
        londonStart.millisecondOfDay || londonEnd.millisecondOfDay ||
        draft.bounds.beginUtcMs % octopus_detail::halfHourMs || draft.bounds.endUtcMs % octopus_detail::halfHourMs)
        return "next Octopus window must use London local midnight and UTC half-hour boundaries";

    if (registerOnly) {
        if (!earlier.readings.registerEnd) return "the saved consumption bundle has no absolute closing meter register";
        draft.bytes = "{\"schema_version\":2,\"scope\":" + jsonQuote(earlier.declaration.opening.evidence.scope) +
            ",\"register_readings\":[{\"utc_ms\":" + begin + ",\"imported_kwh\":" +
            jsonQuote(link.registerEnd) + "}]}\n";
        return nullptr;
    }

    auto &plan = nextTariffField(root, "plan");
    auto &opening = nextTariffField(root, "opening");
    auto &revision = nextTariffField(plan, "revisions").items.front();
    const auto key = "octopus-next-" + link.archiveSha256;
    const auto end = std::to_string(draft.bounds.endUtcMs);
    nextTariffField(root, "source_reference").text = "Prepared from a verified preceding Octopus review";
    nextTariffField(plan, "key").text = key;
    nextTariffField(revision, "key").text = key + "-prices";
    nextTariffField(revision, "begin_utc_ms").text = begin;
    nextTariffField(revision, "end_utc_ms").text = end;
    nextTariffField(opening, "key").text = key + "-opening";
    nextTariffField(opening, "cycle_begin_utc_ms").text = begin;
    nextTariffField(opening, "cycle_end_utc_ms").text = end;
    nextTariffField(opening, "through_utc_ms").text = begin;

    // Included-price annotations are historical statements. They cannot establish the next cycle's tax details.
    if (plan.find("tax_periods")) {
        draft.olderTaxAnnotations = true;
        plan.members.erase(std::remove_if(plan.members.begin(), plan.members.end(), [](const auto &entry) {
            return entry.first == "tax_periods" || entry.first == "tax_allocation";
        }), plan.members.end());
        opening.members.erase(std::remove_if(opening.members.begin(), opening.members.end(), [](const auto &entry) {
            return entry.first == "tax_bases" || entry.first == "tax_segments";
        }), opening.members.end());
        nextTariffField(root, "schema_version").text = earlier.declaration.resetSchedule->annualMonth ? "5" : "4";
    }

    TariffDeclaration validated;
    if (!decodeTariffDeclaration(root, &validated)) return "could not verify the next tariff declaration";
    if (const auto *problem = checkOctopusRateDeclaration(validated)) return problem;
    const PublicJsonRequest request{PublicFeed::OctopusUnitRates, draft.bounds.beginUtcMs / 1000,
        draft.bounds.endUtcMs / 1000, {}, OctopusRatePage{validated.plan.product, validated.plan.territory.front(), 1}};
    std::string url;
    draft.publicCapture = publicFeedUrl(request, &url);
    draft.bytes = tariffJson(root) + '\n';
    return nullptr;
}

const char *checkLiveCycleInputs(const ReviewOriginal &original, const MeterReview &review,
                                 const std::string &payment, const std::string &output) {
    if (!original.previousCycle) return "previous Octopus cycle is missing";
    if (const auto *problem = verifyCycleLink(original, review, &payment)) return problem;
    const auto &series = std::get<std::vector<MeterImportInterval>>(review.readings.series);
    if (std::any_of(series.begin(), series.end(), [](const auto &interval) {
        return interval.beginUtcMs % octopus_detail::halfHourMs ||
               interval.endUtcMs - interval.beginUtcMs != octopus_detail::halfHourMs;
    })) return electricityStatusName(ElectricityStatus::MeterRoundingBoundary);
    if (!original.invoice.empty()) {
        MeterInvoice invoice;
        if (const auto *problem = decodeMeterInvoice(original.invoice, review.declaration, invoice)) return problem;
        const auto &opening = review.declaration.opening;
        const auto matches = [&](const InvoicePeriod &period) {
            return period.begin == opening.cycleBeginUtcMs && period.end == opening.cycleEndUtcMs;
        };
        if (invoice.independent) {
            const auto &rows = invoice.independent->supply.periods;
            if (rows.size() != 1 || !matches(rows.front().period))
                return "supply statement must match a complete reviewed billing cycle from a zero opening";
        } else {
            if (invoice.cycles.size() != 1) return "invoice must list one charge for each reviewed cycle";
            if (!matches(invoice.cycles.front().period)) return "invoice cycle bounds differ from the reviewed cycle";
        }
    }
    const auto target = localReviewPath(output);
    if (!target) return "review output path is unavailable";
    std::error_code error;
    auto temporary = *target;
    temporary += ".tmp";
    for (const auto &path : {*target, temporary}) {
        if (std::filesystem::symlink_status(path, error).type() != std::filesystem::file_type::not_found)
            return "review output path is unavailable or already exists";
    }
    return nullptr;
}

const char *captureMeterReviewRates(ReviewOriginal &original, int64_t &now, const char *lateReadingError,
                                    const std::string &payment, const std::string &dns, const std::string &output,
                                    const OctopusRateCaptureSource &source, NativeProcessLifetime &lifetime,
                                    const market_detail::MarketSampleClock &started,
                                    market_detail::MarketSampleClock &lastSample, const volatile sig_atomic_t *stop,
                                    const std::function<bool()> &stopped) {
    using namespace market_detail;
    MeterReview preview;
    if (const auto *problem = prepareMeterReview(original, now, false, lateReadingError, stopped, preview))
        return problem;
    if (const auto *problem = checkLiveCycleInputs(original, preview, payment, output)) return problem;
    const auto before = marketTradeClock();
    if (!marketTradeClockContinuous(started, lastSample, before))
        return "local clock changed during the billing review";
    lastSample = before;
    if (stopped() || !lifetime.begin()) return "helper ownership uncertain";

    OctopusRateCapture captured;
    if (const auto *problem = source.fetch(std::move(preview.declaration), payment, dns, source.module,
                                           lifetime, stop, captured, &lastSample)) return problem;
    const auto finished = marketTradeClock();
    if (!marketTradeClockContinuous(started, lastSample, finished) ||
        captured.summary.observedUtcMs < static_cast<int64_t>(before.second) ||
        captured.summary.observedUtcMs > static_cast<int64_t>(finished.second))
        return "local clock changed during the billing review";
    lastSample = finished;
    now = static_cast<int64_t>(finished.second);
    original.cutoff = captured.summary.observedUtcMs;
    original.supplierRates = std::move(captured.bytes);
    return nullptr;
}

LocalFileRead readCycleSlot(const std::string &name, size_t limit, std::string &out) {
    const auto path = localReviewPath(name);
    if (!path) return LocalFileRead::Unreadable;
    const auto state = readBoundedLocalFile(*path, limit, &out);
    return state == LocalFileRead::Ok && out.empty() ? LocalFileRead::Invalid : state;
}

bool appendConsumptionUtc(int64_t utcMs, std::string &bundle) {
    constexpr int64_t secondsPerDay = 86400;
    const auto seconds = utcMs / 1000;
    const auto date = civilDateFromSerialDay(seconds / secondsPerDay);
    if (!date) return false;
    const auto time = static_cast<unsigned>(seconds % secondsPerDay);
    char timestamp[21];
    if (std::snprintf(timestamp, sizeof(timestamp), "%04d-%02u-%02uT%02u:%02u:%02uZ",
                      date->year, date->month, date->day, time / 3600, time / 60 % 60, time % 60) != 20)
        return false;
    bundle.append(timestamp, 20);
    return true;
}

const char *loadOctopusImportPages(const std::filesystem::path &directory, const std::string &scope,
                                   const TariffCycleBounds &window, int64_t cutoff, bool annual,
                                   const std::function<bool()> &stopped, std::string &bundle) {
    constexpr size_t maxPages = 192, pageBytes = 64 * 1024;
    using octopus_detail::halfHourMs;
    const auto begin = window.beginUtcMs, end = window.endUtcMs;
    const auto limit = annual ? maxAnnualMeterIntervals : maxOrdinaryMeterIntervals;
    if (begin < 0 || end > cutoff || end <= begin || begin % halfHourMs || end % halfHourMs ||
        static_cast<uint64_t>((end - begin) / halfHourMs) > limit)
        return "Octopus import pages require a complete elapsed half-hour billing window";
    const auto expected = static_cast<size_t>((end - begin) / halfHourMs);

    const auto marker = localReviewPath(directory / "observation.json");
    std::error_code error;
    if (!marker || std::filesystem::symlink_status(marker->parent_path(), error).type() != std::filesystem::file_type::directory)
        return "Octopus consumption page directory is unavailable";
    std::string bytes;
    JsonValue observation;
    int64_t observed = 0;
    const auto markerState = readBoundedLocalFile(*marker, 1024, &bytes);
    if (markerState == LocalFileRead::Missing) return nullptr;
    if (markerState != LocalFileRead::Ok || !parseJsonStrict(bytes, &observation, nullptr, {1024, 1}) ||
        !octopus_detail::fields(observation, {"observed_utc_ms"}) ||
        !instant(observation.find("observed_utc_ms"), observed) || observed < end || observed > cutoff)
        return "Octopus page observation is invalid or outside the review cutoff";

    JsonValue rows;
    rows.type = JsonValue::Type::Array;
    rows.items.reserve(expected);
    std::string meterRoute;
    size_t pageSize = 0;
    for (size_t index = 1; rows.items.size() < expected; ++index) {
        if (stopped()) return "review cancelled";
        if (index > maxPages) return "Octopus consumption exceeds the page limit";
        const auto path = marker->parent_path() / ("page-" + std::to_string(index) + ".json");
        JsonValue page;
        if (!readLocalReviewInput(path, pageBytes, bytes) || !parseJsonStrict(bytes, &page, nullptr, {pageBytes, 4}) ||
            !octopus_detail::appendConsumptionPage(page, index, expected, pageSize, meterRoute, rows))
            return "invalid or incomplete Octopus consumption pages";
    }
    std::vector<MeterImportInterval> verified;
    if (const auto *problem = octopus_detail::decodeConsumption(rows, begin, end, verified, stopped)) return problem;
    auto text = "{\"schema_version\":3,\"scope\":" + jsonQuote(scope) +
        ",\"end_utc_ms\":" + std::to_string(end) + ",\"observed_utc_ms\":" + std::to_string(observed) +
        ",\"consumption\":[";
    const auto byteLimit = annual ? maxMeterFileBytes : maxOrdinaryMeterFileBytes;
    for (const auto &reading : verified) {
        if (stopped()) return "review cancelled";
        if (reading.beginUtcMs != begin) text += ',';
        text += "{\"interval_start\":\"";
        if (!appendConsumptionUtc(reading.beginUtcMs, text)) return "invalid Octopus UTC boundary";
        text += "\",\"interval_end\":\"";
        if (!appendConsumptionUtc(reading.endUtcMs, text)) return "invalid Octopus UTC boundary";
        std::string quantity;
        if (!reading.importedKwh.format(3, &quantity)) return "Octopus consumption exceeds arithmetic capacity";
        text += "\",\"consumption\":" + quantity + '}';
        if (text.size() > byteLimit - 3) return "Octopus consumption bundle exceeds the size limit";
    }
    text += "]}\n";
    bundle = std::move(text);
    return nullptr;
}

bool recurringNameMatches(std::string_view lower, std::string_view name) {
    return lower.size() == name.size() && std::equal(lower.begin(), lower.end(), name.begin(), [](char expected, char actual) {
        if (actual >= 'A' && actual <= 'Z') actual = static_cast<char>(actual - 'A' + 'a');
        return expected == actual;
    });
}

int64_t recurringCycleClosingFromName(std::string_view basename) {
    constexpr std::string_view prefix = "cycle-", suffix = ".cmr";
    if (basename.size() <= prefix.size() + suffix.size() ||
        basename.size() > prefix.size() + suffix.size() + 15)
        return 0;
    if (!recurringNameMatches(prefix, basename.substr(0, prefix.size())) ||
        !recurringNameMatches(suffix, basename.substr(basename.size() - suffix.size())))
        return 0;
    const auto *first = basename.data() + prefix.size();
    const auto *last = basename.data() + basename.size() - suffix.size();
    if (*first < '1' || *first > '9') return 0;
    int64_t closing = 0;
    const auto parsed = std::from_chars(first, last, closing);
    return parsed.ec == std::errc{} && parsed.ptr == last && closing <= meterBillMaxUtcMs ? closing : 0;
}

int64_t recurringComparisonClosingFromName(std::string_view basename) {
    constexpr std::string_view suffix = ".compared.cmr";
    if (basename.size() <= suffix.size() ||
        !recurringNameMatches(suffix, basename.substr(basename.size() - suffix.size()))) return 0;
    return recurringCycleClosingFromName(std::string(basename.substr(0, basename.size() - suffix.size())) + ".cmr");
}

class RecurringWorkspaceLease {
   public:
    bool acquire(const std::filesystem::path &lock) {
#if !defined(_WIN32)
        // An unlinked lock file can be recreated while its original inode remains leased.
        if (!directory_.acquireDirectory(lock.parent_path())) return false;
#endif
        return file_.acquire(lock);
    }
   private:
#if !defined(_WIN32)
    LocalStoreLease directory_;
#endif
    LocalStoreLease file_;
};

std::optional<int64_t> latestCycleArchiveEnd(const std::filesystem::path &directory,
                                               const std::function<bool()> &stopped) {
    int64_t latest = 0;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(directory, error), end; !error && entry != end; entry.increment(error)) {
        if (stopped()) return std::nullopt;
        const auto name = entry->path().filename().u8string();
        latest = (std::max)(latest, recurringCycleClosingFromName(std::string(name.begin(), name.end())));
    }
    if (error) return std::nullopt;
    return latest;
}

const char *verifyInvoiceSeedSlot(const std::filesystem::path &seed, const std::filesystem::path &workspace,
                                  const PreviousMeterCycle &link, const std::function<bool()> &stopped) {
    const auto seedName = seed.filename().u8string();
    const auto namedClosing = recurringCycleClosingFromName(std::string(seedName.begin(), seedName.end()));
    if (namedClosing && namedClosing != link.closedAt) return "seed filename differs from its verified closing time";

    std::error_code error;
    for (std::filesystem::directory_iterator entry(workspace, error), end; !error && entry != end; entry.increment(error)) {
        if (stopped()) return "review cancelled";
        const auto name = entry->path().filename().u8string();
        const std::string filename(name.begin(), name.end());
        std::string_view basename(filename);
        if (basename.size() > 4 && recurringNameMatches(".tmp", basename.substr(basename.size() - 4))) {
            basename.remove_suffix(4);
            if (recurringCycleClosingFromName(basename) == link.closedAt)
                return "canonical seed archive has uncertain temporary output";
            continue;
        }
        if (recurringCycleClosingFromName(basename) != link.closedAt) continue;
        std::string original;
        if (!readLocalReviewInput(entry->path(), maxArchiveBytes, original) || sha256Hex(original) != link.archiveSha256)
            return "canonical cycle archive does not match the selected seed";
    }
    return error ? "could not inspect canonical seed archive names" : nullptr;
}

const char *verifyInvoiceSuccessor(const ReviewOriginal &original, const PreviousMeterCycle &link,
                                   const std::filesystem::path &workspace, int64_t now, const char *lateReadingError,
                                   const std::function<bool()> &stopped) {
    JsonValue tariff;
    TariffCycleBounds bounds;
    if (const auto *problem = nextOctopusCycleBounds(original, link.closedAt, tariff, bounds)) {
        const auto latest = latestCycleArchiveEnd(workspace, stopped);
        if (!latest) return "could not inspect recurring review archive names";
        return *latest > link.closedAt ? problem : nullptr;
    }
    const auto target = "cycle-" + std::to_string(bounds.endUtcMs) + ".cmr";
    std::string bytes;
    auto state = readCycleSlot(target, maxArchiveBytes, bytes);
    if (state == LocalFileRead::Missing) {
        const auto latest = latestCycleArchiveEnd(workspace, stopped);
        const auto temporary = localReviewPath(target + ".tmp");
        if (!latest || !temporary) return "could not inspect recurring review archive names";
        std::error_code error;
        const auto tempType = std::filesystem::symlink_status(*temporary, error).type();
        state = readCycleSlot(target, maxArchiveBytes, bytes);
        if (state == LocalFileRead::Missing) {
            if (*latest >= bounds.endUtcMs) return "recurring review archive gap; restore the existing archive chain before continuing";
            return tempType == std::filesystem::file_type::not_found ? nullptr :
                "recurring review successor has uncertain temporary output";
        }
    }
    ReviewOriginal next;
    if (state != LocalFileRead::Ok || !decodeReviewOriginal(bytes, next))
        return "recurring review successor archive is unavailable or invalid";
    if (!next.previousCycle || !sameClosingRecord(*next.previousCycle, link))
        return "recurring review successor does not match the previous closing record";
    MeterReview review;
    if (const auto *problem = replaySavedReview(next, now, lateReadingError, stopped, review)) return problem;
    return review.declaration.opening.cycleEndUtcMs == bounds.endUtcMs ? nullptr :
        "recurring review successor closes at a different time";
}

struct RecurringCycleProgress {
    size_t saved = 0;
    size_t invoicesSaved = 0;
    int64_t closing = 0;
    bool historyLimit = false;
};

struct RecurringCycleWatch {
    std::string seedDigest, latestName, latestDigest;
    std::map<int64_t, std::string> comparedDigests;
    market_detail::MarketSampleClock lastSample;
};

std::string cycleComparisonName(int64_t closing) {
    return "cycle-" + std::to_string(closing) + ".compared.cmr";
}

constexpr auto watchedComparisonChanged = "a previously verified invoice comparison changed or disappeared while watching";

bool sameCycleReviewInputs(const ReviewOriginal &a, const ReviewOriginal &b) {
    if (a.cutoff != b.cutoff || a.tariff != b.tariff || a.intervals != b.intervals ||
        a.supplierRates != b.supplierRates || a.prepaid != b.prepaid ||
        a.previous.has_value() != b.previous.has_value() ||
        a.previousCycle.has_value() != b.previousCycle.has_value()) return false;
    if (a.previous && (a.previous->cutoff != b.previous->cutoff || a.previous->through != b.previous->through ||
        a.previous->archiveSha256 != b.previous->archiveSha256 || a.previous->prefixSha256 != b.previous->prefixSha256))
        return false;
    return !a.previousCycle || sameClosingRecord(*a.previousCycle, *b.previousCycle);
}

const char *readLateCycleInvoice(const ReviewOriginal &original, int64_t closing, int64_t now,
                                const char *lateReadingError, const std::function<bool()> &stopped,
                                InvoiceReviewDetails *details, std::string *retainedInvoice = nullptr,
                                std::string *archiveDigest = nullptr) {
    const auto name = cycleComparisonName(closing);
    std::string bytes;
    auto state = readCycleSlot(name, maxArchiveBytes, bytes);
    if (state == LocalFileRead::Missing) {
        const auto temporary = localReviewPath(name + ".tmp");
        std::error_code error;
        if (!temporary || std::filesystem::symlink_status(*temporary, error).type() != std::filesystem::file_type::not_found)
            return "late invoice review has uncertain temporary output";
        // A publisher may have moved the temporary file after the first read.
        state = readCycleSlot(name, maxArchiveBytes, bytes);
        if (state == LocalFileRead::Missing) return nullptr;
    }
    ReviewOriginal comparison;
    if (state != LocalFileRead::Ok || !decodeReviewOriginal(bytes, comparison) || comparison.invoice.empty())
        return "late invoice review unavailable or invalid";
    if (!original.invoice.empty()) return "cycle already contains an archived invoice comparison";
    if (!sameCycleReviewInputs(original, comparison))
        return "late invoice review does not match the original cycle inputs";
    MeterReview review;
    std::string meterOnlyExact;
    if (const auto *problem = replaySavedReview(comparison, now, lateReadingError, stopped, review,
                                                  &meterOnlyExact, nullptr, details)) return problem;
    if (meterOnlyExact != original.results) return "late invoice review changed the original cycle amounts";
    if (retainedInvoice) *retainedInvoice = std::move(comparison.invoice);
    if (archiveDigest) *archiveDigest = sha256Hex(bytes);
    return nullptr;
}

const char *prepareStagedCycleInvoice(const ReviewOriginal &original, int64_t closing, int64_t now,
                                      const char *lateReadingError, const std::function<bool()> &stopped,
                                      bool &retained, std::string &output, std::string *archiveDigest) {
    std::string retainedInvoice;
    if (const auto *problem = readLateCycleInvoice(original, closing, now, lateReadingError, stopped,
                                                   nullptr, &retainedInvoice, archiveDigest)) return problem;
    const auto &savedInvoice = original.invoice.empty() ? retainedInvoice : original.invoice;
    retained = !savedInvoice.empty();

    std::string staged;
    const auto state = readCycleSlot("cycle-" + std::to_string(closing) + ".invoice.json", maxInvoiceBytes, staged);
    if (state == LocalFileRead::Missing) return nullptr;
    if (state != LocalFileRead::Ok) return "staged invoice is unavailable, empty, oversized or not a regular file";
    if (retained) return staged == savedInvoice ? nullptr : "staged invoice differs from the saved comparison";

    auto comparison = original;
    comparison.invoice = std::move(staged);
    MeterReview review;
    if (const auto *problem = prepareMeterReview(comparison, now, false, lateReadingError, stopped, review)) return problem;
    std::string meterOnlyExact;
    if (const auto *problem = calculateMeterReview(comparison, true, stopped, review, &meterOnlyExact)) return problem;
    if (meterOnlyExact != original.results) return "staged invoice review changed the original cycle amounts";
    if (review.exact.size() > maxExactResultsBytes) return "exact results exceed the archive bound";
    comparison.results = std::move(review.exact);
    output = encodeReviewOriginal(comparison);
    return nullptr;
}

struct CycleInvoiceHistory {
    size_t reviews = 0, comparableCycles = 0;
    size_t aboveCycles = 0, belowCycles = 0;
    InvoiceTotals totals;
    ExactDecimal aboveTotal, belowTotal;
    unsigned places = 0;
};

const char *appendCycleHistory(const ReviewOriginal &original, const MeterReview &review,
                               const std::vector<CompletedSupplyCycle> &periods, ElectricityAmount &total,
                               const InvoiceReviewDetails *details, CycleInvoiceHistory &invoices, std::string &rows) {
    if (periods.size() != 1) return "cycle history requires a complete supply cycle";
    const auto &cycle = periods.front();
    std::string amount;
    if (!cycle.charge.format(6, &amount) || !total.numerator.add(cycle.charge.numerator, &total.numerator))
        return "cycle history amount exceeds arithmetic capacity";
    rows += "[" + std::to_string(cycle.period.begin) + ", " + std::to_string(cycle.period.end) + ") UTC ms: " +
        amount + " " + review.declaration.plan.currency;
    if (!original.invoice.empty()) rows += " (archived invoice comparison)";
    else if (details) rows += details->report.empty() ? " (no invoice review)" : " (late invoice comparison)";
    rows += '\n';
    if (details && !details->report.empty()) {
        ++invoices.reviews;
        rows += details->report;
        if (details->supply) {
            const auto &supply = *details->supply;
            ExactDecimal difference;
            if (!invoices.totals.calculated.add(supply.totals.calculated, &invoices.totals.calculated) ||
                !invoices.totals.stated.add(supply.totals.stated, &invoices.totals.stated) ||
                !supply.totals.calculated.subtract(supply.totals.stated, &difference))
                return "cycle invoice totals exceed arithmetic capacity";
            if (difference.negative()) {
                if (!invoices.belowTotal.subtract(difference, &invoices.belowTotal))
                    return "cycle invoice totals exceed arithmetic capacity";
                ++invoices.belowCycles;
            } else if (!difference.zero()) {
                if (!invoices.aboveTotal.add(difference, &invoices.aboveTotal))
                    return "cycle invoice totals exceed arithmetic capacity";
                ++invoices.aboveCycles;
            }
            ++invoices.comparableCycles;
            invoices.places = (std::max)(invoices.places, supply.places);
        }
    }
    return nullptr;
}

const char *describeOctopusReadingsPlan(const MeterReview &prior, const PreparedOctopusCycle &draft,
                                      const std::string &previousDigest, const std::string &seedDigest,
                                      RecurringMode mode, bool available,
                                      std::string &report) {
    const auto begin = draft.bounds.beginUtcMs;
    const auto end = draft.bounds.endUtcMs;
    if (prior.readings.version != 3 ||
        (end - begin) / octopus_detail::halfHourMs > static_cast<int64_t>(maxAnnualMeterIntervals))
        return "account capture requires an eligible Octopus half-hour import cycle";
    const auto stem = "cycle-" + std::to_string(end);
    bool ready = available;
    if (available) {
        using FileType = std::filesystem::file_type;
        for (const auto &[suffix, expected] : {std::pair{".readings.json", FileType::regular},
                                              std::pair{".pages", FileType::directory}}) {
            const auto path = localReviewPath(stem + suffix);
            if (!path) return "could not inspect staged readings";
            std::error_code error;
            const auto type = std::filesystem::symlink_status(*path, error).type();
            if (type == FileType::not_found) continue;
            if (mode == RecurringMode::NextReadings || type != expected)
                return "readings are already staged or unavailable";
            ready = false;
        }
    }
    report = "{\"schema_version\":1,\"scope\":" + jsonQuote(prior.declaration.opening.evidence.scope) +
        ",\"begin_utc_ms\":" + std::to_string(begin) + ",\"end_utc_ms\":" + std::to_string(end) +
        ",\"predecessor_sha256\":" + jsonQuote(previousDigest);
    if (mode == RecurringMode::ScheduledReadings)
        report += ",\"seed_sha256\":" + jsonQuote(seedDigest) + ",\"ready\":" + (ready ? "true" : "false");
    report += "}\n";
    return nullptr;
}

const char *rollOctopusCycles(const std::filesystem::path &seed, const std::filesystem::path &workspace,
                             int64_t requestedCutoff, int64_t now,
                             bool confirmImport,
                             const OctopusRateCaptureSource *source, const std::string &dns,
                             NativeProcessLifetime &lifetime, const market_detail::MarketSampleClock &started,
                             const volatile sig_atomic_t *stop, const std::function<bool()> &stopped,
                             RecurringCycleWatch *watch, RecurringMode mode, bool withInvoices, bool watchInvoices,
                             RecurringCycleProgress &progress, std::string &report) {
    constexpr size_t maxHistory = 120;
    constexpr size_t maxNew = 24;
    constexpr size_t maxCaptures = 3;
    constexpr auto lateReadingError = "interval ends after the review cutoff";
    ReviewOriginal previous;
    MeterReview priorReview;
    std::string bytes;
    if (!readLocalReviewInput(seed, maxArchiveBytes, bytes) || !decodeReviewOriginal(bytes, previous))
        return "recurring review seed archive unavailable or invalid";
    auto previousDigest = sha256Hex(bytes);
    const bool history = mode == RecurringMode::History;
    const bool reconciling = mode == RecurringMode::ReconcileInvoices;
    const bool compareInvoices = reconciling || watchInvoices;
    const bool closedOnly = history || reconciling;
    const bool existingOnly = mode != RecurringMode::None;
    const bool refreshInspection = closedOnly || mode == RecurringMode::ScheduledReadings;
    const auto seedDigest = mode == RecurringMode::ScheduledReadings ? previousDigest : std::string{};
    if (watch) {
        if (!watch->seedDigest.empty() && watch->seedDigest != previousDigest)
            return "recurring review seed changed while watching";
        watch->seedDigest = previousDigest;
    }
    if (watchInvoices) {
        std::string saved;
        if (!watch->latestName.empty() &&
            (readCycleSlot(watch->latestName, maxArchiveBytes, saved) != LocalFileRead::Ok ||
             sha256Hex(saved) != watch->latestDigest))
            return "latest verified recurring archive changed or disappeared while watching";
        for (const auto &[closing, digest] : watch->comparedDigests) {
            if (stopped()) return "review cancelled";
            if (readCycleSlot(cycleComparisonName(closing), maxArchiveBytes, saved) != LocalFileRead::Ok ||
                sha256Hex(saved) != digest) {
                progress.closing = closing;
                return watchedComparisonChanged;
            }
        }
    }
    bool sawWatchedHead = !watch || watch->latestName.empty();
    if (requestedCutoff && requestedCutoff < previous.cutoff) return "new review cutoff precedes the saved cutoff";
    if (requestedCutoff > now) return "review cutoff is in the future";
    std::vector<CompletedSupplyCycle> periods;
    ElectricityAmount historyTotal;
    std::string historyRows;
    InvoiceReviewDetails invoiceDetails;
    CycleInvoiceHistory invoiceHistory;
    if (const auto *problem = replaySavedReview(previous, now, lateReadingError, stopped, priorReview,
                                                 nullptr, history ? &periods : nullptr,
                                                 withInvoices ? &invoiceDetails : nullptr))
        return problem;

    auto phaseSample = started;
    auto &lastSample = watch ? watch->lastSample : phaseSample;
    const auto refreshClock = [&] {
        if (!source && !watch && !refreshInspection) return true;
        const auto current = market_detail::marketTradeClock();
        if (!market_detail::marketTradeClockContinuous(started, lastSample, current) ||
            current.second < static_cast<uint64_t>(now)) return false;
        lastSample = current;
        now = static_cast<int64_t>(current.second);
        return true;
    };
    size_t existing = 0, captures = 0;
    size_t retainedInvoices = 0, missingInvoices = 0;
    std::string latestName;
    auto previousPath = seed;
    std::string_view waiting;
    std::optional<int64_t> lastExistingClosing;
    const auto comparisonMatchesWatch = [&](int64_t closing, const std::string &digest) {
        if (!watchInvoices) return true;
        const auto prior = watch->comparedDigests.find(closing);
        return prior == watch->comparedDigests.end() || prior->second == digest;
    };
    const auto reconcilePrevious = [&](int64_t closing, bool endOfChain) -> const char * {
        progress.closing = closing;
        bool retained = false;
        std::string comparison, digest;
        if (const auto *problem = prepareStagedCycleInvoice(previous, closing, now, lateReadingError,
                                                           stopped, retained, comparison,
                                                           watchInvoices ? &digest : nullptr)) return problem;
        if (!comparisonMatchesWatch(closing, digest)) return watchedComparisonChanged;
        if (!comparison.empty()) {
            if (std::filesystem::current_path() != workspace)
                return "recurring review workspace moved during invoice reconciliation";
            std::string original;
            if (!readLocalReviewInput(previousPath, maxArchiveBytes, original) || sha256Hex(original) != previousDigest)
                return "original cycle archive changed before invoice publication";
            if (previousPath == seed) {
                const auto link = bindPreviousCycle(previous, priorReview, previousDigest);
                if (!link) return "could not bind the seed before invoice publication";
                if (const auto *problem = verifyInvoiceSeedSlot(seed, workspace, *link, stopped)) return problem;
            }
            if (endOfChain && !progress.historyLimit) {
                const auto latest = latestCycleArchiveEnd(workspace, stopped);
                if (!latest) return "could not inspect recurring review archive names";
                if (*latest > closing) return "a successor archive appeared before invoice publication; rerun reconciliation";
            }
            if (!refreshClock()) return "local clock changed during the billing review";
            const auto target = cycleComparisonName(closing);
            const auto saved = local_review_archive::saveWhileActive(target, ".cmr", comparison,
                                                                     maxArchiveBytes, stopped);
            if (saved == local_review_archive::ReviewSave::Stopped) return "review cancelled";
            if (saved == local_review_archive::ReviewSave::Uncertain)
                return "invoice comparison publication or cleanup is uncertain";
            if (saved != local_review_archive::ReviewSave::Written)
                return "could not create an invoice comparison; existing archives were preserved";
            ++progress.invoicesSaved;
            if (watchInvoices) digest = sha256Hex(comparison);
        } else if (retained) ++retainedInvoices;
        else ++missingInvoices;
        if (watchInvoices && !digest.empty()) watch->comparedDigests.try_emplace(closing, std::move(digest));
        progress.closing = 0;
        return nullptr;
    };
    const auto invoiceSummary = [&] {
        return "Octopus invoice reconciliation: " + std::to_string(progress.invoicesSaved) + " saved, " +
            std::to_string(retainedInvoices) + " already compared, " + std::to_string(missingInvoices) +
            " without a staged invoice\n";
    };
    for (;;) {
        progress.closing = 0;
        if (stopped()) return "review cancelled";
        if (!refreshClock()) return "local clock changed during the billing review";
        const auto cutoff = requestedCutoff ? requestedCutoff : now;
        const auto link = bindPreviousCycle(previous, priorReview, previousDigest);
        if (!link) return "previous review must be a complete recurring Octopus cycle and matching rate window";
        if (previous.cutoff > cutoff) return "new review cutoff precedes the saved cutoff";
        if (history) {
            if (withInvoices) {
                progress.closing = link->closedAt;
                if (const auto *problem = readLateCycleInvoice(previous, link->closedAt, now, lateReadingError, stopped,
                                                                &invoiceDetails)) return problem;
                progress.closing = 0;
            }
            if (const auto *problem = appendCycleHistory(previous, priorReview, periods, historyTotal,
                                                         withInvoices ? &invoiceDetails : nullptr, invoiceHistory, historyRows))
                return problem;
        } else if (compareInvoices && existing == 0 && progress.saved == 0) {
            progress.closing = link->closedAt;
            if (const auto *problem = verifyInvoiceSeedSlot(seed, workspace, *link, stopped)) return problem;
            progress.closing = 0;
        }
        if (existing + progress.saved >= maxHistory) {
            if (!closedOnly && existingOnly)
                return "archive replay limit reached; use the latest verified archive as the seed";
            progress.historyLimit = true;
            waiting = "archive replay limit reached; use the latest verified archive as the next seed";
            break;
        }

        PreparedOctopusCycle draft;
        if (closedOnly) {
            JsonValue tariff;
            if (const auto *problem = nextOctopusCycleBounds(previous, link->closedAt, tariff, draft.bounds)) {
                const auto latest = latestCycleArchiveEnd(workspace, stopped);
                if (!latest) return "could not inspect recurring review archive names";
                if (*latest > link->closedAt) return problem;
                waiting = "No successor window available under the saved reset rule";
                break;
            }
        } else if (const auto *problem = prepareNextOctopusCycle(previous, priorReview, *link, false, draft)) {
            return problem;
        }
        progress.closing = draft.bounds.endUtcMs;
        if (!closedOnly && progress.closing > cutoff) {
            if (mode == RecurringMode::NextReadings) return "the next billing cycle has not closed";
            if (mode == RecurringMode::ScheduledReadings)
                return describeOctopusReadingsPlan(priorReview, draft, previousDigest, seedDigest, mode, false, report);
            waiting = "waiting for the cycle to close";
            break;
        }
        const auto stem = "cycle-" + std::to_string(progress.closing);
        const auto target = stem + ".cmr";
        using FileType = std::filesystem::file_type;
        const auto temporaryType = [&] {
            const auto temporary = localReviewPath(target + ".tmp");
            std::error_code error;
            return temporary ? std::filesystem::symlink_status(*temporary, error).type() : FileType::none;
        };
        auto archive = readCycleSlot(target, maxArchiveBytes, bytes);
        auto observedTemporary = FileType::none;
        if (archive == LocalFileRead::Missing && existingOnly) {
            lastExistingClosing = latestCycleArchiveEnd(workspace, stopped);
            if (!lastExistingClosing) return "could not inspect recurring review archive names";
            // The watcher may have moved the temporary archive after the directory scan.
            observedTemporary = temporaryType();
            archive = readCycleSlot(target, maxArchiveBytes, bytes);
        }
        if (watch && target == watch->latestName) {
            if (archive != LocalFileRead::Ok || sha256Hex(bytes) != watch->latestDigest)
                return "latest verified recurring archive changed or disappeared while watching";
            sawWatchedHead = true;
        }
        ReviewOriginal next;
        MeterReview nextReview;
        if (archive == LocalFileRead::Ok) {
            if (!decodeReviewOriginal(bytes, next)) return "recurring review successor archive is invalid";
            if (!next.previousCycle || !sameClosingRecord(*next.previousCycle, *link))
                return "recurring review successor does not match the previous closing record";
            if (refreshInspection && !refreshClock())
                return "local clock changed while reading the recurring successor";
            if (next.cutoff > (refreshInspection ? now : cutoff))
                return "successor archive is newer than the requested cutoff";
            periods.clear();
            invoiceDetails = {};
            if (const auto *problem = replaySavedReview(next, now, lateReadingError, stopped, nextReview,
                                                         nullptr, history ? &periods : nullptr,
                                                         withInvoices ? &invoiceDetails : nullptr))
                return problem;
            if (nextReview.declaration.opening.cycleEndUtcMs != progress.closing)
                return "recurring review successor closes at a different time";
            ++existing;
        } else {
            if (archive != LocalFileRead::Missing) return "recurring review successor archive is unavailable";
            if (!sawWatchedHead) return "a previously verified recurring archive disappeared while watching";
            if (!lastExistingClosing) lastExistingClosing = latestCycleArchiveEnd(workspace, stopped);
            if (!lastExistingClosing) return "could not inspect recurring review archive names";
            if (*lastExistingClosing >= progress.closing)
                return "recurring review archive gap; restore the existing archive chain before continuing";
            const auto tempType = existingOnly ? observedTemporary : temporaryType();
            if (mode == RecurringMode::ScheduledReadings && tempType == FileType::regular)
                return describeOctopusReadingsPlan(priorReview, draft, previousDigest, seedDigest, mode, false, report);
            if (tempType != FileType::not_found)
                return "recurring review successor has uncertain temporary output";
            if (closedOnly) {
                waiting = "End of verified archive chain";
                break;
            }
            if (existingOnly)
                return describeOctopusReadingsPlan(priorReview, draft, previousDigest, seedDigest, mode, true, report);
            if (progress.saved >= maxNew) {
                waiting = "new archive limit reached; run again to continue";
                break;
            }
            if (existing == 0 && progress.saved == 0)
                if (const auto *problem = verifyInvoiceSeedSlot(seed, workspace, *link, stopped)) return problem;
            next.cutoff = cutoff;
            next.tariff = std::move(draft.bytes);
            next.previousCycle = link;
            const auto readings = readCycleSlot(stem + ".readings.json", maxMeterFileBytes, next.intervals);
            if (readings == LocalFileRead::Missing) {
                if (confirmImport) {
                    const auto directory = localReviewPath(stem + ".pages");
                    if (!directory) return "Octopus consumption page directory is unavailable";
                    std::error_code directoryError;
                    const auto type = std::filesystem::symlink_status(*directory, directoryError).type();
                    if (type == std::filesystem::file_type::directory) {
                        if (priorReview.readings.version != 3)
                            return "Octopus pages cannot continue a cumulative-register archive";
                        const auto &priorTariff = priorReview.declaration;
                        // A producer may publish a complete export during this check. Verify its observation
                        // against the refreshed watch cutoff after reading the inputs.
                        const auto pageCutoff = watch ? meterBillMaxUtcMs : cutoff;
                        if (const auto *problem = loadOctopusImportPages(*directory, priorTariff.opening.evidence.scope,
                                draft.bounds, pageCutoff, priorTariff.resetSchedule->annualMonth != 0,
                                stopped, next.intervals)) return problem;
                    } else if (type != std::filesystem::file_type::not_found) {
                        return "Octopus consumption page directory is unavailable";
                    }
                }
                if (next.intervals.empty()) {
                    waiting = "waiting for complete meter readings";
                    break;
                }
            } else if (readings != LocalFileRead::Ok) {
                return "recurring review readings are unavailable or invalid";
            }
            const auto rates = readCycleSlot(stem + ".rates.json", maxOctopusRateFileBytes, next.supplierRates);
            if (rates == LocalFileRead::Missing && !source) {
                waiting = "waiting for a saved Octopus rate capture";
                break;
            }
            if (rates != LocalFileRead::Ok && rates != LocalFileRead::Missing)
                return "recurring review rate capture is unavailable or invalid";
            const auto invoice = readCycleSlot(stem + ".invoice.json", maxInvoiceBytes, next.invoice);
            if (invoice != LocalFileRead::Missing && invoice != LocalFileRead::Ok)
                return "recurring review invoice is unavailable or invalid";
            if (watch) {
                if (!refreshClock()) return "local clock changed during the billing review";
                next.cutoff = now;
            }

            if (rates == LocalFileRead::Missing) {
                if (captures >= maxCaptures) {
                    waiting = "public capture limit reached; run again to continue";
                    break;
                }
                if (!draft.publicCapture) return "next cycle exceeds the installed public rate capture bounds";
                if (watch && std::filesystem::current_path() != workspace)
                    return "recurring review workspace moved while watching";
                if (const auto *problem = captureMeterReviewRates(next, now, lateReadingError,
                        priorReview.supplier->paymentMethod, dns, target, *source, lifetime, started, lastSample,
                        stop, stopped))
                    return problem;
                ++captures;
            }
            if (const auto *problem = prepareMeterReview(next, now, false, lateReadingError, stopped, nextReview))
                return problem;
            if (const auto *problem = verifyCycleLink(next, nextReview)) return problem;
            if (const auto *problem = calculateMeterReview(next, true, stopped, nextReview)) return problem;
            if (nextReview.exact.size() > maxExactResultsBytes) return "exact results exceed the archive bound";
            next.results = std::move(nextReview.exact);
            std::string priorInvoiceDigest;
            if (const auto *problem = readLateCycleInvoice(previous, link->closedAt, now, lateReadingError, stopped,
                                                           nullptr, nullptr,
                                                           watchInvoices ? &priorInvoiceDigest : nullptr)) return problem;
            if (!comparisonMatchesWatch(link->closedAt, priorInvoiceDigest)) {
                progress.closing = link->closedAt;
                return watchedComparisonChanged;
            }
            if (const auto *problem = readLateCycleInvoice(next, progress.closing, now, lateReadingError, stopped,
                                                           nullptr)) return problem;
            if (stopped()) return "review cancelled";
            if (!refreshClock()) return "local clock changed during the billing review";
            if (watch && std::filesystem::current_path() != workspace)
                return "recurring review workspace moved while watching";
            bytes = encodeReviewOriginal(next);
            const auto state = local_review_archive::saveWhileActive(target, ".cmr", bytes, maxArchiveBytes, stopped);
            if (state == local_review_archive::ReviewSave::Stopped) return "review cancelled";
            if (state == local_review_archive::ReviewSave::Uncertain)
                return "recurring review publication or cleanup is uncertain";
            if (state != local_review_archive::ReviewSave::Written)
                return "could not create a recurring review archive; existing archives were preserved";
            ++progress.saved;
        }
        // A named seed may share its closing slot with another valid archive. Verify the
        // successor's link before publishing this cycle's comparison under that shared name.
        if (compareInvoices)
            if (const auto *problem = reconcilePrevious(link->closedAt, false)) return problem;
        previousDigest = sha256Hex(bytes);
        if (watch) latestName = target;
        if (compareInvoices) previousPath = workspace / target;
        previous = std::move(next);
        priorReview = std::move(nextReview);
    }
    if (reconciling) {
        if (const auto *problem = reconcilePrevious(priorReview.declaration.opening.cycleEndUtcMs, true)) return problem;
        if (!refreshClock()) return "local clock changed during the billing review";
        report = invoiceSummary() + std::string(waiting) + "\n";
        return nullptr;
    }
    if (history) {
        if (withInvoices && !refreshClock()) return "local clock changed during the billing review";
        std::string total;
        if (!historyTotal.format(6, &total)) return "cycle history amount exceeds the display range";
        report = "Octopus supply estimates from the selected seed (" + std::to_string(existing + 1) +
            (existing ? " verified cycles)\n" : " verified cycle)\n") + historyRows +
            "Estimated total " + total + " " + priorReview.declaration.plan.currency + "\n";
        if (withInvoices) {
            report += "Invoice reviews available for " + std::to_string(invoiceHistory.reviews) + " of " +
                std::to_string(existing + 1) + " verified cycles\n";
            report += "Supply invoice totals cover " + std::to_string(invoiceHistory.comparableCycles) + " of " +
                std::to_string(existing + 1) + " verified cycles\n";
            if (invoiceHistory.comparableCycles) {
                std::string calculated, stated, change, above, below;
                ExactDecimal difference;
                const auto &totals = invoiceHistory.totals;
                const auto places = invoiceHistory.places;
                if (!totals.calculated.formatRatio(1, places, &calculated) ||
                    !totals.stated.formatRatio(1, places, &stated) ||
                    !totals.calculated.subtract(totals.stated, &difference) ||
                    !difference.formatRatio(1, places, &change) ||
                    !invoiceHistory.aboveTotal.formatRatio(1, places, &above) ||
                    !invoiceHistory.belowTotal.formatRatio(1, places, &below))
                    return "cycle invoice totals exceed the display range";
                const auto &currency = priorReview.declaration.plan.currency;
                report += "  Stated supply charges total " + stated + " " + currency + "\n";
                report += "  Calculated at each invoice precision " + calculated + " " + currency + "\n";
                report += "  Total difference (calculated minus invoice) " + change + " " + currency + "\n";
                const auto matches = invoiceHistory.comparableCycles - invoiceHistory.aboveCycles - invoiceHistory.belowCycles;
                report += "  Supply invoice matches: " + std::to_string(matches) + " of " +
                    std::to_string(invoiceHistory.comparableCycles) + " compared cycles\n";
                const auto appendDiscrepancy = [&](const char *label, size_t cycles, const std::string &amount) {
                    report += label + std::to_string(cycles) + (cycles == 1 ? " cycle, " : " cycles, ") +
                        amount + " " + currency + " total\n";
                };
                appendDiscrepancy("  Calculated above invoice: ", invoiceHistory.aboveCycles, above);
                appendDiscrepancy("  Calculated below invoice: ", invoiceHistory.belowCycles, below);
            }
        }
        report += std::string(waiting) + "\n";
        return nullptr;
    }

    if (!sawWatchedHead) return "could not recheck the latest verified recurring archive while watching";
    const auto nextClosing = progress.closing;
    if (watchInvoices) {
        if (const auto *problem = reconcilePrevious(priorReview.declaration.opening.cycleEndUtcMs, true)) return problem;
        if (!refreshClock()) return "local clock changed during the billing review";
    }
    if (watch && !latestName.empty()) {
        watch->latestName = std::move(latestName);
        watch->latestDigest = std::move(previousDigest);
    }
    report = "Recurring Octopus review: " + std::to_string(existing) + " verified, " +
        std::to_string(progress.saved) + " saved\nLast verified closing " +
        std::to_string(priorReview.declaration.opening.cycleEndUtcMs) + " UTC ms\n";
    report += waiting;
    if (nextClosing && existing + progress.saved < maxHistory)
        report += "\nNext closing " + std::to_string(nextClosing) + " UTC ms";
    report += '\n';
    if (watchInvoices) report += invoiceSummary();
    return nullptr;
}

int refuse(const char *reason) {
    std::fprintf(stderr, "meter-bill: %s\n", reason);
    return 3;
}

}  // namespace

int commandMeterBill(const MeterBillOptions &options, int argc, const char *const *argv,
                     const volatile sig_atomic_t *stop, const OctopusRateCaptureSource *publicRates) {
    if (options.help) {
        std::puts("canary-miner meter-bill --tariff FILE --intervals FILE [--octopus-rates FILE] [--as-of-utc-ms MS]\n"
                  "    [--invoice FILE] [--prepaid FILE] [--save-review NAME.cmr]\n"
                  "canary-miner meter-bill --open-review FILE [--invoice FILE [--save-review NAME.cmr]] [--previous-cycle ARCHIVE]\n"
                  "canary-miner meter-bill --prepare-next-cycle ARCHIVE (--save-tariff NAME.json | --save-register NAME.json)\n"
                  "canary-miner meter-bill --next-cycle-review ARCHIVE --intervals FILE --save-review NAME.cmr\n"
                  "    --octopus-rates FILE [--tariff FILE] [--as-of-utc-ms MS] [--invoice FILE]\n"
                  "canary-miner meter-bill --next-cycle-review ARCHIVE --intervals FILE --save-review NAME.cmr\n"
                  "    --allow-public-rates [--dns-server IP|cloudflare-doh] [--invoice FILE]\n"
                  "canary-miner meter-bill --roll-forward SEED.cmr [--confirm-electricity-import]\n"
                  "    [--watch-every-seconds SECONDS [--watch-quiet] [--watch-invoices]]\n"
                  "    [--as-of-utc-ms MS | --allow-public-rates [--dns-server IP|cloudflare-doh]]\n"
                  "canary-miner meter-bill --next-readings-plan SEED.cmr\n"
                  "canary-miner meter-bill --scheduled-readings-plan SEED.cmr\n"
                  "canary-miner meter-bill --cycle-history SEED.cmr [--with-invoices]\n"
                  "canary-miner meter-bill --reconcile-invoices SEED.cmr\n"
                  "canary-miner meter-bill --import-octopus-pages DIR --tariff FILE --save-readings NAME.json\n"
                  "    --confirm-electricity-import\n"
                  "canary-miner meter-bill --extend-review FILE --intervals FILE --save-review NAME.cmr\n"
                  "    [--as-of-utc-ms MS] [--invoice FILE] [--prepaid FILE]\n"
                  "Review a saved combined tariff against contiguous whole-meter intervals.\n"
                  "Accepts average-watt intervals or cumulative whole-meter import readings. Prints an estimate to stdout.\n"
                  "For a saved kVA tariff, include apparent VA, a permitted power factor, or paired kWh/kVAh registers.\n"
                  "For export payment, supply signed average net watts or paired import/export registers.\n"
                  "Also accepts local Octopus electricity import bundles in schema version 3.\n"
                  "Register readings need billing-cycle boundaries and any energy-rate or tax boundaries that split nonzero use.\n"
                  "Half-hour rounding uses complete UTC half hours. Without demand, give one reading per boundary.\n"
                  "With demand, finer readings are grouped. Add a reading wherever either grid splits nonzero import.\n"
                  "Demand requires complete aligned windows. Add a register reading wherever a demand boundary splits nonzero import.\n"
                  "Use --octopus-rates to apply captured public Agile or Go prices to a compatible tariff declaration.\n"
                  "Use --prepare-next-cycle to write a tariff declaration or a starting register from a verified Octopus review.\n"
                  "Use --next-cycle-review for a complete adjacent Octopus billing cycle with a new rate capture.\n"
                  "Omit --tariff to derive it from the archive. Live capture requires --allow-public-rates.\n"
                  "Roll forward uses cycle-END.readings.json, cycle-END.rates.json and optional cycle-END.invoice.json.\n"
                  "END is the cycle closing UTC millisecond. Each verified cycle commits to cycle-END.cmr.\n"
                  "Use --next-readings-plan in the seed's workspace to identify the next closed cycle that needs readings.\n"
                  "Watch checks again after 1 to 86400 seconds. It requires the current clock and holds the workspace lease.\n"
                  "Use --watch-quiet for a supervised watch without a standard output stream.\n"
                  "Use --watch-invoices to compare late staged invoices while watching in the seed's workspace.\n"
                  "With electricity import confirmed, use cycle-END.pages if the readings file is absent.\n"
                  "Page directories contain observation.json and page-1.json, page-2.json, and so on.\n"
                  "Use --as-of-utc-ms to require readings and published rates available by that UTC instant.\n"
                  "Use --invoice to compare supply charges, reconcile independent export statements, or review interim annual charges.\n"
                  "Save optionally writes the private inputs and exact results to a new archive in the current folder.\n"
                  "Open rechecks the saved inputs and results at their original cutoff.\n"
                  "To retain a late invoice comparison, use --invoice with --save-review and a new archive name.\n"
                  "Reconcile saves staged invoices for already verified cycles to separate cycle-END.compared.cmr files.\n"
                  "Extend verifies the saved review and requires the complete interval history with additional readings.");
        return 0;
    }
    const bool live = options.allowPublicRatesArg != 0;
    const bool reconciling = options.recurringMode == RecurringMode::ReconcileInvoices;
    const bool watchInvoices = options.watchInvoicesArg != 0;
    NativeProcessLifetime captureLifetime;
    const std::function<bool()> stopped = [&] {
        return market_detail::marketSampleStopped(stop) || (captureLifetime.bound() && !captureLifetime.permitted());
    };
    const auto stopResult = [&]() {
        if (!options.saveArg && !options.archiveArg && !options.invoiceArg && !options.prepareCycleArg &&
            !options.rollForwardArg && !options.importPagesArg && !options.recurringModeArg) return 0;
        std::fputs(options.importPagesArg ? "meter-bill: Octopus page import stopped before publication\n" :
                   options.recurringMode == RecurringMode::History ? "meter-bill: cycle history cancelled\n" :
                   reconciling ? "meter-bill: invoice reconciliation stopped; saved comparisons remain available\n" :
                   options.recurringModeArg ? "meter-bill: readings plan cancelled\n" :
                   options.rollForwardArg ? "meter-bill: recurring review stopped; saved cycles remain available\n" :
                   options.prepareCycleArg ? "meter-bill: next-cycle preparation stopped before publication\n" :
                   options.saveArg ? "meter-bill: archive save stopped before publication\n" :
                   options.archiveArg ? "meter-bill: saved review verification stopped\n" :
                                        "meter-bill: invoice comparison stopped\n", stderr);
        return 130;
    };
    const auto fail = [&](const char *reason) {
        if (captureLifetime.bound() && !captureLifetime.permitted()) return refuse("helper ownership uncertain");
        return stopped() ? stopResult() : refuse(reason);
    };
    try {
        const bool watching = options.watchArg != 0;
        const bool timed = live || watching || options.recurringMode == RecurringMode::ScheduledReadings ||
            options.recurringMode == RecurringMode::History || reconciling;
        const auto started = timed ? market_detail::marketTradeClock() : market_detail::MarketSampleClock{};
        auto now = static_cast<int64_t>(timed ? started.second : market_detail::marketUtcClockMs());
        if (stopped()) return stopResult();
        const auto paths = meterBillPaths(options, argc, argv);
        if (!paths) return fail(options.reviewCutoffArg ? "invalid review cutoff or input file arguments" :
                                                         "could not resolve input file arguments");
        if (live && !publicRates) return fail("public HTTPS unavailable in this build");
        if (live && (!publicRates->fetch || publicRates->module.empty()))
            return fail("installed helper path unavailable");
        if (options.importPagesArg) {
            std::string tariff, bundle;
            TariffDeclaration declaration;
            bool namedTariff = false;
            if (!readLocalReviewInput(paths->tariff, tariffDeclarationMaxBytes, tariff))
                return fail("saved tariff declaration unavailable or invalid");
            if (const auto *problem = decodeMeterTariff(tariff, declaration, namedTariff)) return fail(problem);
            if (const auto *problem = checkOctopusRateDeclaration(declaration)) return fail(problem);
            const auto &opening = declaration.opening;
            if (opening.throughUtcMs != opening.cycleBeginUtcMs || declaration.plan.exportPayment ||
                declaration.plan.revisions.front().beginUtcMs != opening.cycleBeginUtcMs ||
                declaration.plan.revisions.front().endUtcMs != opening.cycleEndUtcMs ||
                static_cast<int64_t>(declaration.confirmedUtc * 1000) > now)
                return fail("Octopus page import requires a full import-only tariff cycle");
            const TariffCycleBounds window{opening.cycleBeginUtcMs, opening.cycleEndUtcMs};
            if (const auto *problem = loadOctopusImportPages(paths->importPages, opening.evidence.scope, window, now,
                    declaration.resetSchedule && declaration.resetSchedule->annualMonth, stopped, bundle))
                return fail(problem);
            if (bundle.empty()) return fail("Octopus page observation is invalid or missing");
            const auto state = local_review_archive::saveWhileActive(options.outputName, ".json", bundle, maxMeterFileBytes, stopped);
            if (state == local_review_archive::ReviewSave::Stopped) return stopResult();
            if (state == local_review_archive::ReviewSave::Uncertain)
                return refuse("consumption bundle publication or cleanup is uncertain");
            if (state != local_review_archive::ReviewSave::Written)
                return refuse("could not create a consumption bundle; any existing file was preserved");
            return std::printf("Saved Octopus electricity imports for closing %lld UTC ms\n",
                               static_cast<long long>(window.endUtcMs)) < 0 || std::fflush(stdout) != 0 ? 1 : 0;
        }
        if (options.rollForwardArg || options.recurringModeArg) {
            const bool readOnly = options.recurringModeArg != 0 && !reconciling;
            const auto leasePath = localReviewPath("meter-billing.lock");
            constexpr auto unavailable = "recurring review workspace is busy or unavailable";
            if (!leasePath) return fail(unavailable);
            const auto workspace = leasePath->parent_path();
            if (options.recurringModeArg || watchInvoices) {
                std::error_code error;
                const auto parent = std::filesystem::absolute(paths->archive, error).parent_path();
                if (error || !std::filesystem::equivalent(parent, workspace, error))
                    return fail(reconciling || watchInvoices ? "run invoice reconciliation in the seed archive's workspace" :
                                options.recurringMode == RecurringMode::History ?
                                "run cycle history in the seed archive's workspace" :
                                "run the readings plan in the seed archive's workspace");
            }
            RecurringWorkspaceLease lease;
            if (!readOnly && !lease.acquire(*leasePath)) return fail(unavailable);
            RecurringCycleWatch watch;
            watch.lastSample = started;
            auto previousCheck = started;
            std::string lastReport;
            for (;;) {
                if (stopped()) return fail("review cancelled");
                if (watching && std::filesystem::current_path() != workspace)
                    return fail("recurring review workspace moved while watching");
                const auto sample = watching ? market_detail::marketTradeClock() : started;
                if (watching && !market_detail::marketTradeClockContinuous(previousCheck, watch.lastSample, sample))
                    return fail("local clock changed while waiting for the next billing check");
                previousCheck = sample;
                watch.lastSample = sample;
                std::string report;
                RecurringCycleProgress progress;
                if (const auto *problem = rollOctopusCycles(paths->archive, workspace,
                        options.reviewCutoffUtcMs, watching ? static_cast<int64_t>(sample.second) : now,
                        options.confirmImportArg != 0, live ? publicRates : nullptr, options.dnsServer,
                        captureLifetime, sample, stop, stopped, watching ? &watch : nullptr, options.recurringMode,
                        options.historyInvoicesArg != 0, watchInvoices, progress, report)) {
                    if (progress.closing)
                        std::fprintf(stderr, "meter-bill: cycle closing %lld UTC ms\n", static_cast<long long>(progress.closing));
                    if (progress.saved)
                        std::fprintf(stderr, "meter-bill: this %s committed %zu new cycle%s\n",
                                     watching ? "check" : "run", progress.saved, progress.saved == 1 ? "" : "s");
                    if (progress.invoicesSaved)
                        std::fprintf(stderr, "meter-bill: this %s saved %zu invoice comparison%s\n",
                                     watching ? "check" : "run", progress.invoicesSaved,
                                     progress.invoicesSaved == 1 ? "" : "s");
                    return fail(problem);
                }
                if ((options.recurringModeArg || watchInvoices) && stopped()) return stopResult();
                if (!options.quietWatchArg && report != lastReport) {
                    if (std::fwrite(report.data(), 1, report.size(), stdout) != report.size() || std::fflush(stdout) != 0)
                        return 1;
                    lastReport = std::move(report);
                }
                if (!watching) return 0;
                if (progress.historyLimit) return refuse("recurring review watch reached its archive replay limit");

                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.watchSeconds);
                for (auto current = std::chrono::steady_clock::now(); !stopped() && current < deadline;
                     current = std::chrono::steady_clock::now())
                    std::this_thread::sleep_until((std::min)(deadline, current + std::chrono::milliseconds(100)));
            }
        }
        ReviewOriginal original;
        std::string inheritedPayment;
        const bool reopening = options.archiveArg != 0;
        const bool extending = options.extendArg != 0;
        const bool nextCycle = options.nextCycleArg != 0;
        const bool preparing = options.prepareCycleArg != 0;
        const bool archiving = options.saveArg != 0;
        const bool savingAttachedInvoice = reopening && archiving;
        const auto namedCycleClosing = archiving ? recurringCycleClosingFromName(options.outputName) : 0;
        const auto namedInvoiceClosing = archiving ? recurringComparisonClosingFromName(options.outputName) : 0;
        if (savingAttachedInvoice && namedCycleClosing)
            return fail("output name is reserved for recurring cycles");
        if (namedInvoiceClosing && !savingAttachedInvoice)
            return fail("a recurring comparison requires an invoice attached to a saved cycle");
        if ((namedCycleClosing && options.outputName != "cycle-" + std::to_string(namedCycleClosing) + ".cmr") ||
            (namedInvoiceClosing && options.outputName != "cycle-" + std::to_string(namedInvoiceClosing) + ".compared.cmr"))
            return fail("use the lowercase recurring archive name");
        RecurringWorkspaceLease publicationLease;
        std::filesystem::path publicationWorkspace;
        if (namedCycleClosing || namedInvoiceClosing) {
            const auto leasePath = localReviewPath("meter-billing.lock");
            if (!leasePath || !publicationLease.acquire(*leasePath))
                return fail("recurring review workspace is busy or unavailable");
            publicationWorkspace = leasePath->parent_path();
            if (namedInvoiceClosing) {
                std::error_code error;
                const auto parent = std::filesystem::absolute(paths->archive, error).parent_path();
                if (error || !std::filesystem::equivalent(parent, publicationWorkspace, error))
                    return fail("save recurring comparisons in the original archive's workspace");
            }
        }
        std::optional<ReviewOriginal> publicationPredecessor;
        std::string attachedCycleDigest;
        const auto cutoff = options.reviewCutoffArg ? options.reviewCutoffUtcMs : now;
        const char *lateReadingError = options.reviewCutoffArg || reopening || extending || nextCycle || preparing
            ? "interval ends after the review cutoff" : "interval ends in the future";
        int64_t previousConsumptionObservation = 0;
        if (reopening || extending || nextCycle || preparing) {
            std::string bytes;
            if (!readLocalReviewInput(paths->archive, maxArchiveBytes, bytes) || !decodeReviewOriginal(bytes, original))
                return fail("review archive unavailable or invalid");
            if (namedInvoiceClosing) attachedCycleDigest = sha256Hex(bytes);
            if (preparing) {
                MeterReview earlier;
                if (const auto *problem = replaySavedReview(original, now, lateReadingError, stopped, earlier))
                    return fail(problem);
                const auto link = bindPreviousCycle(original, earlier, sha256Hex(bytes));
                if (!link) return fail("previous review must be a complete recurring Octopus cycle and matching rate window");
                const bool registerOnly = options.saveRegisterArg != 0;
                PreparedOctopusCycle draft;
                if (const auto *problem = prepareNextOctopusCycle(original, earlier, *link, registerOnly, draft))
                    return fail(problem);
                const auto saved = local_review_archive::save(options.outputName, ".json", draft.bytes,
                                                              tariffDeclarationMaxBytes, stop);
                if (saved == local_review_archive::ReviewSave::Stopped) return stopResult();
                if (saved == local_review_archive::ReviewSave::Uncertain)
                    return refuse("next-cycle file publication or cleanup is uncertain");
                if (saved != local_review_archive::ReviewSave::Written)
                    return refuse("could not create the next-cycle file; any existing file was preserved");

                std::string report = "Prepared next Octopus cycle [" + std::to_string(draft.bounds.beginUtcMs) +
                    ", " + std::to_string(draft.bounds.endUtcMs) + ") UTC ms\n";
                if (registerOnly) report += "Saved the opening register. Append the next cycle's cumulative readings.\n";
                else {
                    report += "Saved a tariff declaration. Payment method " + earlier.supplier->paymentMethod + "\n";
                    if (!draft.publicCapture) report += "The window exceeds the installed public rate capture bounds.\n";
                    else if (draft.bounds.endUtcMs > now) report += "Public rates can be captured after the cycle ends.\n";
                    else report += "Capture the public rates for this window before reviewing the cycle.\n";
                    if (draft.olderTaxAnnotations) report += "Review any included-price tax annotations for the new cycle.\n";
                }
                return std::fwrite(report.data(), 1, report.size(), stdout) == report.size() &&
                    std::fflush(stdout) == 0 ? 0 : 1;
            }
            if (extending || nextCycle) {
                if (extending && original.previousCycle)
                    return fail("use next-cycle review to continue a closed Octopus cycle");
                if (extending && static_cast<bool>(options.prepaidArg) != !original.prepaid.empty())
                    return fail("prepaid continuation requires the same review format and complete purchase history");
                if (cutoff < original.cutoff) return fail("new review cutoff precedes the saved cutoff");
                MeterReview earlier;
                std::string meterExact;
                if (const auto *problem = replaySavedReview(original, now, lateReadingError, stopped, earlier,
                                                             extending ? &meterExact : nullptr))
                    return fail(problem);
                if (nextCycle) {
                    auto link = bindPreviousCycle(original, earlier, sha256Hex(bytes));
                    if (!link) return fail("previous review must be a complete recurring Octopus cycle and matching rate window");
                    PreparedOctopusCycle draft;
                    if (!options.tariffArg) {
                        if (const auto *problem = prepareNextOctopusCycle(original, earlier, *link, false, draft))
                            return fail(problem);
                        if (live && !draft.publicCapture) return fail("next cycle exceeds the installed public rate capture bounds");
                    }
                    if (live) inheritedPayment = earlier.supplier->paymentMethod;
                    if (namedCycleClosing) publicationPredecessor = std::move(original);
                    original = {};
                    original.tariff = std::move(draft.bytes);
                    original.previousCycle = std::move(link);
                } else {
                    auto &previous = original.previous.emplace();
                    previous.cutoff = original.cutoff;
                    previous.archiveSha256 = sha256Hex(bytes);
                    std::visit([&](const auto &series) {
                        previous.through = series.back().endUtcMs;
                        previous.prefixSha256 = meterPrefixDigest(earlier.readings, series, meterExact);
                    }, earlier.readings.series);
                    previousConsumptionObservation = earlier.readings.octopusObserved;
                    original.invoice.clear();
                }
            }
        }
        if (!reopening) original.cutoff = cutoff;
        if (!reopening && !extending && options.tariffArg &&
            !readLocalReviewInput(paths->tariff, tariffDeclarationMaxBytes, original.tariff))
            return fail("saved tariff declaration unavailable or invalid");
        if (stopped()) return stopResult();
        if (!reopening && !readLocalReviewInput(paths->intervals, maxMeterFileBytes, original.intervals))
            return fail("interval file unavailable, oversized or not a regular file");
        if (options.ratesArg &&
            (!readLocalReviewInput(paths->rates, maxOctopusRateFileBytes, original.supplierRates) ||
             original.supplierRates.empty()))
            return fail("Octopus rate file unavailable, empty, oversized or not a regular file");

        if (options.prepaidArg &&
            (!readLocalReviewInput(paths->prepaid,maxPrepaidFileBytes,original.prepaid) || original.prepaid.empty()))
            return fail("prepaid file unavailable, empty, oversized or not a regular file");

        if (live) {
            if (options.invoiceArg &&
                (!readLocalReviewInput(paths->invoice, maxInvoiceBytes, original.invoice) || original.invoice.empty()))
                return fail("invoice file unavailable, empty, oversized or not a regular file");
            auto lastSample = started;
            if (const auto *problem = captureMeterReviewRates(original, now, lateReadingError, inheritedPayment,
                    options.dnsServer, options.outputName, *publicRates, captureLifetime, started, lastSample,
                    stop, stopped))
                return fail(problem);
        }

        MeterReview review;
        if (const auto *problem = prepareMeterReview(original, now, options.reviewCutoffArg || reopening || archiving,
                                                      lateReadingError, stopped, review)) return fail(problem);
        if (review.readings.octopusObserved < previousConsumptionObservation)
            return fail("new Octopus consumption observation precedes the saved observation");
        if (const auto *problem = verifyMeterPrefix(original, review, stopped)) return fail(problem);
        if (original.previous)
            review.report += "Extends verified meter coverage through " + std::to_string(original.previous->through) +
                             " UTC ms (previous archive SHA-256 " + original.previous->archiveSha256 + ")\n";
        if (const auto *problem = verifyCycleLink(original, review)) return fail(problem);
        if (options.previousCycleArg)
            if (const auto *problem = verifyPreviousCycleArchive(paths->previousCycle, original, now, lateReadingError, stopped))
                return fail(problem);
        if (original.previousCycle)
            review.report += "Previous Octopus cycle closed at " + std::to_string(original.previousCycle->closedAt) +
                             " UTC ms (archive SHA-256 " + original.previousCycle->archiveSha256 + ")\n";
        const bool retainedInvoice = !live && !original.invoice.empty();
        if (options.invoiceArg && !live) {
            if (retainedInvoice) return fail("saved review already contains an invoice comparison");
            if (!readLocalReviewInput(paths->invoice, maxInvoiceBytes, original.invoice) || original.invoice.empty())
                return fail("invoice file unavailable, empty, oversized or not a regular file");
        }
        // An attached invoice adds exact comparison rows to the new archive. Check
        // the input archive against the meter results before admitting those rows.
        std::string meterOnlyExact;
        if (const auto *problem = calculateMeterReview(original, archiving || retainedInvoice, stopped, review,
                                                       savingAttachedInvoice ? &meterOnlyExact : nullptr))
            return fail(problem);
        if (stopped()) return fail("review cancelled");
        if (reopening && original.results != (savingAttachedInvoice ? meterOnlyExact : review.exact))
            return fail("saved review amounts differ from the recalculated amounts");
        if (archiving) {
            if (review.exact.size() > maxExactResultsBytes) return fail("exact results exceed the archive bound");
            original.results = std::move(review.exact);
            const auto archiveBytes = encodeReviewOriginal(original);
            if ((namedCycleClosing || namedInvoiceClosing) && std::filesystem::current_path() != publicationWorkspace)
                return fail("recurring review workspace moved before archive publication");
            if (namedInvoiceClosing) {
                const auto link = bindPreviousCycle(original, review, attachedCycleDigest);
                if (!link || link->closedAt != namedInvoiceClosing)
                    return fail("recurring comparison filename must match a verified complete cycle closing time");
                std::string originalBytes;
                if (!readLocalReviewInput(paths->archive, maxArchiveBytes, originalBytes) ||
                    sha256Hex(originalBytes) != attachedCycleDigest)
                    return fail("original cycle archive changed before invoice publication");
                if (const auto *problem = verifyInvoiceSeedSlot(paths->archive, publicationWorkspace, *link, stopped))
                    return fail(problem);
                if (const auto *problem = verifyInvoiceSuccessor(original, *link, publicationWorkspace,
                                                                 now, lateReadingError, stopped)) return fail(problem);
            }
            if (namedCycleClosing) {
                const auto link = bindPreviousCycle(original, review, sha256Hex(archiveBytes));
                if (!link || link->closedAt != namedCycleClosing)
                    return fail("recurring archive filename must match a verified complete cycle closing time");
                if (const auto *problem = verifyInvoiceSeedSlot(options.outputName, publicationWorkspace, *link, stopped))
                    return fail(problem);
                if (const auto *problem = verifyInvoiceSuccessor(original, *link, publicationWorkspace,
                                                                 now, lateReadingError, stopped)) return fail(problem);
            }
            if (publicationPredecessor) {
                if (const auto *problem = verifyInvoiceSeedSlot(paths->archive, publicationWorkspace,
                                                                *original.previousCycle, stopped)) return fail(problem);
                if (const auto *problem = readLateCycleInvoice(*publicationPredecessor, original.previousCycle->closedAt,
                                                               now, lateReadingError, stopped, nullptr)) return fail(problem);
            }
            if (namedCycleClosing)
                if (const auto *problem = readLateCycleInvoice(original, namedCycleClosing, now, lateReadingError, stopped,
                                                               nullptr)) return fail(problem);
            const auto state = local_review_archive::saveWhileActive(options.outputName, ".cmr", archiveBytes,
                                                                     maxArchiveBytes, stopped);
            if (state == local_review_archive::ReviewSave::Stopped) return fail("review cancelled");
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
