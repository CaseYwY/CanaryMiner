#pragma once

#include "meter_bill.h"
#include "tariff_declaration.h"

namespace om::meter_bill_detail {

struct InvoiceCycle {
    int64_t begin = 0, end = 0;
    ExactDecimal amount;
};

struct MeterInvoice {
    unsigned places = 0;
    DecimalTies ties = DecimalTies::ToEven;
    std::vector<InvoiceCycle> cycles;
};

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
    if (!schema || !schema->asUint64(&version) || version != 1 || !scope || !scope->isString() ||
        !currency || !currency->isString() || !precision || !precision->asUint64(&places) || places > 6 ||
        !rounding || !rounding->isString() ||
        (rounding->text != "half_even" && rounding->text != "half_away_from_zero") ||
        !cycles || !cycles->isArray() || cycles->items.empty() || cycles->items.size() > maxTariffComparisonCycles)
        return "invalid invoice comparison document";
    if (scope->text != declaration.opening.evidence.scope || currency->text != declaration.plan.currency)
        return "invoice scope or currency differs from the saved tariff";

    invoice.places = static_cast<unsigned>(places);
    if (rounding->text == "half_away_from_zero") invoice.ties = DecimalTies::AwayFromZero;
    invoice.cycles.reserve(cycles->items.size());
    for (const auto &row : cycles->items) {
        const auto *begin = row.find("begin_utc_ms");
        const auto *end = row.find("end_utc_ms");
        const auto *amount = row.find("supply_charge");
        uint64_t a = 0, b = 0;
        InvoiceCycle cycle;
        if (!row.isObject() || row.members.size() != 3 || !begin || !begin->asUint64(&a) ||
            !end || !end->asUint64(&b) || b <= a || b > meterBillMaxUtcMs ||
            !amount || !amount->isString() || !ExactDecimal::parse(amount->text, &cycle.amount) ||
            cycle.amount.digits() > 38 || cycle.amount.scale() > places ||
            (!invoice.cycles.empty() && a != static_cast<uint64_t>(invoice.cycles.back().end)))
            return "invalid invoice cycle or supply charge";
        cycle.begin = static_cast<int64_t>(a);
        cycle.end = static_cast<int64_t>(b);
        invoice.cycles.push_back(std::move(cycle));
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
    if (cycle.begin != opening.cycleBeginUtcMs || cycle.end != opening.cycleEndUtcMs)
        return "invoice cycle bounds differ from the reviewed cycle";

    std::string estimate, stated, change;
    ExactDecimal rounded, difference;
    if (!accrual.settledTotal->numerator.formatRatio(wattMsPerKwh, invoice.places, invoice.ties, &estimate) ||
        !ExactDecimal::parse(estimate, &rounded) ||
        !cycle.amount.formatRatio(1, invoice.places, &stated) || !rounded.subtract(cycle.amount, &difference) ||
        !difference.formatRatio(1, invoice.places, &change))
        return "invoice comparison amount exceeds arithmetic capacity";

    report += "  Invoice supply charge " + stated + " " + currency + "\n";
    report += "  Calculated at invoice precision " + estimate + " " + currency + "\n";
    report += "  Difference (calculated minus invoice) " + change + " " + currency +
        (difference.zero() ? " (match)\n" : " (mismatch)\n");
    if (exact) {
        const char *ties = invoice.ties == DecimalTies::ToEven ? "half_even" : "half_away_from_zero";
        *exact += "[\"invoice\"," + std::to_string(cycle.begin) + "," + std::to_string(cycle.end) +
            "," + std::to_string(invoice.places) + "," + jsonQuote(ties) + "," + jsonQuote(estimate) +
            "," + jsonQuote(stated) + "," + jsonQuote(change) + "]\n";
    }
    return nullptr;
}

}  // namespace om::meter_bill_detail
