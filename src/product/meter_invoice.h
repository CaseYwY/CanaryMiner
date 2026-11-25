#pragma once

#include "meter_bill.h"
#include "tariff_declaration.h"

#include <variant>

namespace om::meter_bill_detail {

struct InvoicePeriod {
    int64_t begin = 0, end = 0;
};

struct InterimSupplyCharge {
    InvoicePeriod period;
    ExactDecimal amount;
};

struct AnnualInvoice {
    std::vector<InterimSupplyCharge> interim;
    std::optional<ExactDecimal> statedAdjustment;
};

struct InvoiceCycle {
    InvoicePeriod period;
    std::variant<ExactDecimal, AnnualInvoice> charge;
};

struct MeterInvoice {
    unsigned places = 0;
    DecimalTies ties = DecimalTies::ToEven;
    bool annual = false;
    std::vector<InvoiceCycle> cycles;
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
        InterimSupplyCharge charge;
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
        !root.isObject() || root.members.size() != 6)
        return "invalid invoice comparison document";
    const auto *schema = root.find("schema_version");
    const auto *scope = root.find("scope");
    const auto *currency = root.find("currency");
    const auto *precision = root.find("decimal_places");
    const auto *rounding = root.find("rounding");
    const auto *cycles = root.find("cycles");
    uint64_t version = 0, places = 0;
    if (!schema || !schema->asUint64(&version) || (version != 1 && version != 2) || !scope || !scope->isString() ||
        !currency || !currency->isString() || !precision || !precision->asUint64(&places) || places > 6 ||
        !rounding || !rounding->isString() ||
        (rounding->text != "half_even" && rounding->text != "half_away_from_zero") ||
        !cycles || !cycles->isArray() || cycles->items.empty() || cycles->items.size() > maxTariffComparisonCycles)
        return "invalid invoice comparison document";
    if (scope->text != declaration.opening.evidence.scope || currency->text != declaration.plan.currency)
        return "invoice scope or currency differs from the saved tariff";

    invoice.annual = version == 2;
    if (invoice.annual && (!declaration.resetSchedule || !declaration.resetSchedule->annualMonth))
        return "annual invoice comparison requires an annual tariff reset";
    invoice.places = static_cast<unsigned>(places);
    if (rounding->text == "half_away_from_zero") invoice.ties = DecimalTies::AwayFromZero;
    invoice.cycles.reserve(cycles->items.size());
    for (const auto &row : cycles->items) {
        InvoiceCycle cycle;
        if (!row.isObject() || !decodeInvoicePeriod(row, cycle.period) ||
            (!invoice.cycles.empty() && cycle.period.begin != invoice.cycles.back().period.end))
            return "invalid invoice cycle or supply charge";
        if (invoice.annual) {
            if (const auto *problem = decodeAnnualInvoice(row, cycle.period, invoice.places,
                                                          cycle.charge.emplace<AnnualInvoice>()))
                return problem;
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
