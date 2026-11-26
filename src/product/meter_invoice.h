#pragma once

#include "meter_bill.h"
#include "tariff_declaration.h"

#include <algorithm>
#include <variant>

namespace om::meter_bill_detail {

struct InvoicePeriod {
    int64_t begin = 0, end = 0;
};

struct InvoiceAmount {
    InvoicePeriod period;
    ExactDecimal amount;
};

struct AnnualInvoice {
    std::vector<InvoiceAmount> interim;
    std::optional<ExactDecimal> statedAdjustment;
};

struct ExportInvoice {
    ExactDecimal supplyCharge, exportPayment;
};

struct InvoiceCycle {
    InvoicePeriod period;
    std::variant<ExactDecimal, AnnualInvoice, ExportInvoice> charge;
};

enum class MeterInvoiceKind { Supply, Annual, Export };

struct InvoicePrecision {
    unsigned places = 0;
    DecimalTies ties = DecimalTies::ToEven;
};

struct StatementSeries {
    InvoicePrecision precision;
    std::vector<InvoiceAmount> periods;
};

struct IndependentInvoice {
    StatementSeries supply, exported;
};

struct MeterInvoice : InvoicePrecision {
    MeterInvoiceKind kind = MeterInvoiceKind::Supply;
    std::vector<InvoiceCycle> cycles;
    std::optional<IndependentInvoice> independent;
};

inline bool decodeInvoicePeriod(const JsonValue &row, InvoicePeriod &period) {
    const auto *begin = row.find("begin_utc_ms");
    const auto *end = row.find("end_utc_ms");
    uint64_t a = 0, b = 0;
    if (!begin || !begin->asUint64(&a) || !end || !end->asUint64(&b) || b <= a || b > meterBillMaxUtcMs)
        return false;
    period = {static_cast<int64_t>(a), static_cast<int64_t>(b)};
    return true;
}

inline bool decodeInvoiceCharge(const JsonValue *value, unsigned places, ExactDecimal &amount) {
    return value && value->isString() && ExactDecimal::parse(value->text, &amount) &&
           amount.digits() <= 38 && amount.scale() <= places;
}

inline bool decodeInvoicePrecision(const JsonValue &row, InvoicePrecision &precision) {
    const auto *places = row.find("decimal_places");
    const auto *rounding = row.find("rounding");
    uint64_t digits = 0;
    if (!places || !places->asUint64(&digits) || digits > 6 || !rounding || !rounding->isString() ||
        (rounding->text != "half_even" && rounding->text != "half_away_from_zero")) return false;
    precision = {static_cast<unsigned>(digits),
                 rounding->text == "half_even" ? DecimalTies::ToEven : DecimalTies::AwayFromZero};
    return true;
}

inline bool decodeStatementSeries(const JsonValue *group, const char *amountField, size_t limit, StatementSeries &series) {
    if (!group || !group->isObject() || group->members.size() != 3 || !decodeInvoicePrecision(*group, series.precision))
        return false;
    const auto *periods = group->find("periods");
    if (!periods || !periods->isArray() || periods->items.size() > limit) return false;
    int64_t previousEnd = 0;
    for (const auto &row : periods->items) {
        InvoiceAmount amount;
        if (!row.isObject() || row.members.size() != 3 || !decodeInvoicePeriod(row, amount.period) ||
            amount.period.begin < previousEnd ||
            !decodeInvoiceCharge(row.find(amountField), series.precision.places, amount.amount)) return false;
        previousEnd = amount.period.end;
        series.periods.push_back(std::move(amount));
    }
    return true;
}

inline const char *decodeAnnualInvoice(const JsonValue &row, const InvoicePeriod &year, unsigned places,
                                       AnnualInvoice &annual) {
    const auto *interim = row.find("interim_supply_charges");
    const auto *adjustment = row.find("true_up_charge");
    if (row.members.size() != 4 || !interim || !interim->isArray() || interim->items.empty() || !adjustment)
        return "invalid annual invoice comparison";
    if (adjustment->type != JsonValue::Type::Null &&
        !decodeInvoiceCharge(adjustment, places, annual.statedAdjustment.emplace()))
        return "invalid stated annual adjustment";
    auto cursor = year.begin;
    for (const auto &item : interim->items) {
        InvoiceAmount charge;
        if (!item.isObject() || item.members.size() != 3 || !decodeInvoicePeriod(item, charge.period) ||
            !decodeInvoiceCharge(item.find("supply_charge"), places, charge.amount))
            return "invalid interim invoice period or supply charge";
        if (charge.period.begin != cursor || charge.period.end > year.end)
            return "interim invoice periods must cover the annual cycle without gaps or overlap";
        cursor = charge.period.end;
        annual.interim.push_back(std::move(charge));
    }
    if (cursor != year.end) return "interim invoice periods must cover the annual cycle without gaps or overlap";
    return nullptr;
}

inline const char *decodeMeterInvoice(const std::string &bytes, const TariffDeclaration &declaration,
                                     MeterInvoice &invoice) {
    JsonValue root;
    if (!parseJsonStrict(bytes, &root, nullptr, {maxInvoiceBytes, 6}) ||
        !root.isObject())
        return "invalid invoice comparison document";
    const auto *schema = root.find("schema_version");
    const auto *scope = root.find("scope");
    const auto *currency = root.find("currency");
    uint64_t version = 0;
    if (!schema || !schema->asUint64(&version) || version < 1 || version > 4 || !scope || !scope->isString() ||
        !currency || !currency->isString() || root.members.size() != (version == 4 ? 5u : 6u))
        return "invalid invoice comparison document";
    const auto *cycles = root.find("cycles");
    if (version != 4 && (!decodeInvoicePrecision(root, invoice) || !cycles || !cycles->isArray() ||
        cycles->items.empty() || cycles->items.size() > maxTariffComparisonCycles))
        return "invalid invoice comparison document";
    if (scope->text != declaration.opening.evidence.scope || currency->text != declaration.plan.currency)
        return "invoice scope or currency differs from the saved tariff";

    if (version == 4) {
        auto &independent = invoice.independent.emplace();
        if (!decodeStatementSeries(root.find("supply"), "supply_charge", maxTariffComparisonCycles, independent.supply))
            return "invalid supply statement periods or precision";
        if (!decodeStatementSeries(root.find("export"), "export_payment", 128, independent.exported))
            return "invalid export statement periods or precision";
        if (independent.supply.periods.empty() && independent.exported.periods.empty())
            return "independent invoice requires at least one statement period";
        if (!independent.exported.periods.empty() && !declaration.plan.exportPayment)
            return "export invoice comparison requires a declared export payment";
        return nullptr;
    }

    invoice.kind = version == 2 ? MeterInvoiceKind::Annual :
                   version == 3 ? MeterInvoiceKind::Export : MeterInvoiceKind::Supply;
    if (invoice.kind == MeterInvoiceKind::Annual && (!declaration.resetSchedule || !declaration.resetSchedule->annualMonth))
        return "annual invoice comparison requires an annual tariff reset";
    if (invoice.kind == MeterInvoiceKind::Export && !declaration.plan.exportPayment)
        return "export invoice comparison requires a declared export payment";
    invoice.cycles.reserve(cycles->items.size());
    for (const auto &row : cycles->items) {
        InvoiceCycle cycle;
        if (!row.isObject() || !decodeInvoicePeriod(row, cycle.period) ||
            (!invoice.cycles.empty() && cycle.period.begin != invoice.cycles.back().period.end))
            return "invalid invoice cycle or supply charge";
        if (invoice.kind == MeterInvoiceKind::Annual) {
            if (const auto *problem = decodeAnnualInvoice(row, cycle.period, invoice.places,
                                                          cycle.charge.emplace<AnnualInvoice>()))
                return problem;
        } else if (invoice.kind == MeterInvoiceKind::Export) {
            auto &statement = cycle.charge.emplace<ExportInvoice>();
            if (row.members.size() != 4 ||
                !decodeInvoiceCharge(row.find("supply_charge"), invoice.places, statement.supplyCharge) ||
                !decodeInvoiceCharge(row.find("export_payment"), invoice.places, statement.exportPayment))
                return "invalid invoice export payment or supply charge";
        } else if (row.members.size() != 3 ||
                   !decodeInvoiceCharge(row.find("supply_charge"), invoice.places, std::get<ExactDecimal>(cycle.charge))) {
            return "invalid invoice cycle or supply charge";
        }
        invoice.cycles.push_back(std::move(cycle));
    }
    return nullptr;
}

inline const char *appendAnnualReconciliation(const MeterInvoice &invoice, const InvoiceCycle &cycle,
                                             const AnnualInvoice &annual, const ExactDecimal &rounded,
                                             const std::string &estimate, const std::string &currency,
                                             std::string &report, std::string *exact) {
    ExactDecimal alreadyBilled;
    std::string interimRecord = "[";
    for (const auto &charge : annual.interim) {
        std::string display;
        if (!alreadyBilled.add(charge.amount, &alreadyBilled) ||
            !charge.amount.formatRatio(1, invoice.places, &display))
            return "annual invoice amount exceeds arithmetic capacity";
        const auto begin = std::to_string(charge.period.begin);
        const auto end = std::to_string(charge.period.end);
        report += "  Interim supply charge [" + begin + ", " + end + ") UTC ms " + display + " " + currency + "\n";
        if (exact) {
            if (interimRecord.size() > 1) interimRecord += ',';
            interimRecord += '[' + begin + ',' + end + ',' + jsonQuote(display) + ']';
        }
    }

    ExactDecimal adjustment;
    std::string billed, outstanding;
    if (!rounded.subtract(alreadyBilled, &adjustment) ||
        !alreadyBilled.formatRatio(1, invoice.places, &billed) ||
        !adjustment.formatRatio(1, invoice.places, &outstanding))
        return "annual invoice amount exceeds arithmetic capacity";
    report += "  Annual estimate at invoice precision " + estimate + " " + currency + "\n";
    report += "  Interim supply charges total " + billed + " " + currency + "\n";
    report += "  Estimated annual adjustment " + outstanding + " " + currency + "\n";

    std::string stated = "null", comparison = "null";
    if (annual.statedAdjustment) {
        ExactDecimal difference;
        if (!annual.statedAdjustment->formatRatio(1, invoice.places, &stated) ||
            !adjustment.subtract(*annual.statedAdjustment, &difference) ||
            !difference.formatRatio(1, invoice.places, &comparison))
            return "annual invoice amount exceeds arithmetic capacity";
        report += "  Stated annual adjustment " + stated + " " + currency + "\n";
        report += "  Difference (estimated minus stated adjustment) " + comparison + " " + currency +
                  (difference.zero() ? " (match)\n" : " (mismatch)\n");
        stated = jsonQuote(stated);
        comparison = jsonQuote(comparison);
    }
    if (exact) {
        const char *ties = invoice.ties == DecimalTies::ToEven ? "half_even" : "half_away_from_zero";
        *exact += "[\"annual_invoice\"," + std::to_string(cycle.period.begin) + "," +
            std::to_string(cycle.period.end) + "," + std::to_string(invoice.places) + "," + jsonQuote(ties) +
            "," + interimRecord + "]," + jsonQuote(estimate) + "," + jsonQuote(billed) + "," +
            jsonQuote(outstanding) + "," + stated + "," + comparison + "]\n";
    }
    return nullptr;
}

inline bool appendExportInvoiceLine(const char *label, const ExactDecimal &calculated, const ExactDecimal &stated,
                                    unsigned places, const std::string &currency, std::string &report, std::string *exact) {
    std::string estimate, amount, change;
    ExactDecimal difference;
    if (!calculated.formatRatio(1, places, &estimate) || !stated.formatRatio(1, places, &amount) ||
        !calculated.subtract(stated, &difference) || !difference.formatRatio(1, places, &change))
        return false;
    report += std::string("  Invoice ") + label + " " + amount + " " + currency + "\n";
    report += std::string("  Calculated ") + label + " at invoice precision " + estimate + " " + currency + "\n";
    report += std::string("  Difference for ") + label + " (calculated minus invoice) " + change + " " + currency +
        (difference.zero() ? " (match)\n" : " (mismatch)\n");
    if (exact)
        *exact += ",[" + jsonQuote(estimate) + "," + jsonQuote(amount) + "," + jsonQuote(change) + "]";
    return true;
}

struct InvoiceTotals {
    ExactDecimal calculated, stated;
};

inline bool appendStatement(const char *label, const char *record, const InvoicePrecision &precision,
                            const InvoiceAmount &statement, const ElectricityAmount &estimated,
                            const std::string &currency, InvoiceTotals *totals, std::string &report, std::string *exact) {
    ExactDecimal rounded;
    std::string amount;
    if (!estimated.numerator.formatRatio(wattMsPerKwh, precision.places, precision.ties, &amount) ||
        !ExactDecimal::parse(amount, &rounded) || (totals && (!totals->calculated.add(rounded, &totals->calculated) ||
        !totals->stated.add(statement.amount, &totals->stated)))) return false;
    const auto begin = std::to_string(statement.period.begin), end = std::to_string(statement.period.end);
    const char *ties = precision.ties == DecimalTies::ToEven ? "half_even" : "half_away_from_zero";
    report += std::string(label) + " statement [" + begin + ", " + end + ") UTC ms | " +
        std::to_string(precision.places) + " decimal places | " + ties + "\n";
    if (exact)
        *exact += '[' + jsonQuote(record) + ',' + begin + ',' + end + ',' + std::to_string(precision.places) + ',' +
            jsonQuote(ties) + ',' + jsonQuote(estimated.numerator.text());
    if (!appendExportInvoiceLine(label, rounded, statement.amount, precision.places, currency, report, exact)) return false;
    if (exact) *exact += "]\n";
    return true;
}

inline bool continuousStatements(const std::vector<InvoiceAmount> &rows) {
    return !rows.empty() && std::adjacent_find(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        return a.period.end != b.period.begin;
    }) == rows.end();
}

inline std::optional<InvoicePeriod> commonStatementPeriod(const IndependentInvoice &invoice) {
    const auto &a = invoice.supply.periods;
    const auto &b = invoice.exported.periods;
    if (!continuousStatements(a) || !continuousStatements(b) || a.front().period.begin != b.front().period.begin ||
        a.back().period.end != b.back().period.end) return std::nullopt;
    return InvoicePeriod{a.front().period.begin, a.back().period.end};
}

inline bool appendStatementNet(const std::optional<InvoicePeriod> &period, unsigned places, const InvoiceTotals &supply,
                               const InvoiceTotals &exported, const std::string &currency,
                               std::string &report, std::string *exact) {
    if (!period) {
        report += "Statement net comparison unavailable. Supply and export statements must cover the same continuous period.\n";
        if (exact) *exact += "[\"statement_net\",null]\n";
        return true;
    }
    const auto begin = std::to_string(period->begin), end = std::to_string(period->end);
    ExactDecimal calculated, stated;
    if (!supply.calculated.subtract(exported.calculated, &calculated) || !supply.stated.subtract(exported.stated, &stated))
        return false;
    report += "Statement net for [" + begin + ", " + end + ") UTC ms\n";
    if (exact) *exact += "[\"statement_net\"," + begin + ',' + end + ',' + std::to_string(places);
    if (!appendExportInvoiceLine("net electricity cost", calculated, stated, places, currency, report, exact)) return false;
    if (exact) *exact += "]\n";
    return true;
}

inline const char *appendExportInvoiceComparison(const MeterInvoice &invoice, const InvoicePeriod &period,
                                                const ExportInvoice &statement, const BillAccrual &accrual,
                                                const ExactDecimal &roundedSupply, const std::string &currency,
                                                std::string &report, std::string *exact) {
    if (!accrual.exportAccrual) return "export invoice comparison requires complete export readings";
    ExactDecimal roundedExport, calculatedNet, statedNet;
    std::string exportAmount;
    if (!accrual.exportAccrual->paymentTotal.numerator.formatRatio(wattMsPerKwh, invoice.places, invoice.ties, &exportAmount) ||
        !ExactDecimal::parse(exportAmount, &roundedExport) ||
        !roundedSupply.subtract(roundedExport, &calculatedNet) ||
        !statement.supplyCharge.subtract(statement.exportPayment, &statedNet))
        return "invoice comparison amount exceeds arithmetic capacity";

    if (exact) {
        const char *ties = invoice.ties == DecimalTies::ToEven ? "half_even" : "half_away_from_zero";
        *exact += "[\"export_invoice\"," + std::to_string(period.begin) + "," + std::to_string(period.end) +
            "," + std::to_string(invoice.places) + "," + jsonQuote(ties);
    }
    if (!appendExportInvoiceLine("supply charge", roundedSupply, statement.supplyCharge, invoice.places, currency, report, exact) ||
        !appendExportInvoiceLine("export payment", roundedExport, statement.exportPayment, invoice.places, currency, report, exact) ||
        !appendExportInvoiceLine("net electricity cost", calculatedNet, statedNet, invoice.places, currency, report, exact))
        return "invoice comparison amount exceeds arithmetic capacity";
    if (exact) *exact += "]\n";
    return nullptr;
}

inline const char *appendInvoiceComparison(const MeterInvoice &invoice, size_t index, const BillOpening &opening,
                                           const BillAccrual &accrual, const std::string &currency,
                                           std::string &report, std::string *exact) {
    if (index >= invoice.cycles.size()) return "invoice must list one charge for each reviewed cycle";
    if (opening.throughUtcMs != opening.cycleBeginUtcMs || !opening.importedKwh.numerator.zero() ||
        !opening.accruedSubtotal.numerator.zero() || !accrual.settledTotal)
        return "invoice comparison requires complete billing cycles from a zero opening";
    const auto &cycle = invoice.cycles[index];
    if (cycle.period.begin != opening.cycleBeginUtcMs || cycle.period.end != opening.cycleEndUtcMs)
        return "invoice cycle bounds differ from the reviewed cycle";

    std::string estimate;
    ExactDecimal rounded;
    if (!accrual.settledTotal->numerator.formatRatio(wattMsPerKwh, invoice.places, invoice.ties, &estimate) ||
        !ExactDecimal::parse(estimate, &rounded))
        return "invoice comparison amount exceeds arithmetic capacity";
    if (const auto *annual = std::get_if<AnnualInvoice>(&cycle.charge))
        return appendAnnualReconciliation(invoice, cycle, *annual, rounded, estimate, currency, report, exact);
    if (const auto *statement = std::get_if<ExportInvoice>(&cycle.charge))
        return appendExportInvoiceComparison(invoice, cycle.period, *statement, accrual, rounded, currency, report, exact);
    const auto &amount = std::get<ExactDecimal>(cycle.charge);
    std::string stated, change;
    ExactDecimal difference;
    if (!amount.formatRatio(1, invoice.places, &stated) || !rounded.subtract(amount, &difference) ||
        !difference.formatRatio(1, invoice.places, &change))
        return "invoice comparison amount exceeds arithmetic capacity";

    report += "  Invoice supply charge " + stated + " " + currency + "\n";
    report += "  Calculated at invoice precision " + estimate + " " + currency + "\n";
    report += "  Difference (calculated minus invoice) " + change + " " + currency +
        (difference.zero() ? " (match)\n" : " (mismatch)\n");
    if (exact) {
        const char *ties = invoice.ties == DecimalTies::ToEven ? "half_even" : "half_away_from_zero";
        *exact += "[\"invoice\"," + std::to_string(cycle.period.begin) + "," + std::to_string(cycle.period.end) +
            "," + std::to_string(invoice.places) + "," + jsonQuote(ties) + "," + jsonQuote(estimate) +
            "," + jsonQuote(stated) + "," + jsonQuote(change) + "]\n";
    }
    return nullptr;
}

}  // namespace om::meter_bill_detail
