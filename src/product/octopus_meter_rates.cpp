#include "octopus_meter_rates.h"

#include "octopus_json.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace om {
namespace {

using namespace octopus_detail;
constexpr std::size_t maxUnitRows = 8192, maxStandingRows = 256, maxPages = 96;

struct Rate {
    int64_t begin = 0, end = 0;
    ExactDecimal pounds;
};

bool agileProduct(std::string_view product) {
    constexpr std::string_view flex = "AGILE-FLEX-", agile = "AGILE-";
    if (product.substr(0, flex.size()) == flex) product.remove_prefix(flex.size());
    else if (product.substr(0, agile.size()) == agile) product.remove_prefix(agile.size());
    else return false;
    if (product.size() != 8 || product[2] != '-' || product[5] != '-') return false;
    const auto year = digits(product.substr(0, 2));
    return year < 100 && validCivilDate({2000 + static_cast<int>(year), digits(product.substr(3, 2)),
                                       digits(product.substr(6, 2))});
}

bool validLink(const JsonValue &value, bool present) {
    return present ? value.isString() && !value.text.empty() && value.text.size() <= 2048
                   : value.type == JsonValue::Type::Null;
}

bool paymentName(const JsonValue &method) {
    return method.isString() && (method.text == "DIRECT_DEBIT" || method.text == "NON_DIRECT_DEBIT");
}

bool rates(const JsonValue &pages, const std::string &payment, std::size_t maxRows, bool standing,
           std::vector<Rate> &selected, const std::function<bool()> &stop) {
    if (!pages.isArray() || pages.items.empty() || pages.items.size() > maxPages) return false;
    uint64_t count = 0, read = 0;
    for (std::size_t index = 0; index < pages.items.size(); ++index) {
        const auto &page = pages.items[index];
        uint64_t pageCount = 0;
        if (!fields(page, {"count", "next", "previous", "results"}) ||
            !page.find("count")->asUint64(&pageCount) || !pageCount || pageCount > maxRows ||
            (index && pageCount != count) ||
            !validLink(*page.find("next"), index + 1 < pages.items.size()) ||
            !validLink(*page.find("previous"), index != 0))
            return false;
        count = pageCount;
        const auto &rows = *page.find("results");
        if (!rows.isArray() || rows.items.empty() || rows.items.size() > count - read) return false;
        read += rows.items.size();
        for (const auto &entry : rows.items) {
            if (stop && stop()) return false;
            if (!fields(entry, {"value_exc_vat", "value_inc_vat", "valid_from", "valid_to", "payment_method"}))
                return false;
            const auto &method = *entry.find("payment_method");
            const bool generic = method.type == JsonValue::Type::Null;
            if (!generic && !paymentName(method)) return false;
            Rate rate;
            // An open-ended standing charge is clipped to the price file's completely observed window.
            rate.end = meterBillMaxUtcMs;
            const auto &end = *entry.find("valid_to");
            const auto &inclusive = *entry.find("value_inc_vat");
            const auto &exclusive = *entry.find("value_exc_vat");
            ExactDecimal pence, untaxed;
            if (!utcInstant(*entry.find("valid_from"), rate.begin) ||
                (!(standing && end.type == JsonValue::Type::Null) && !utcInstant(end, rate.end)) ||
                rate.end <= rate.begin || !exactNumber(inclusive, pence) || !exactNumber(exclusive, untaxed) ||
                (standing && (pence.negative() || untaxed.negative())) ||
                !pence.divideExact(100, &rate.pounds))
                return false;
            if (generic || method.text == payment) selected.push_back(std::move(rate));
        }
    }
    if (read != count || selected.empty()) return false;
    std::sort(selected.begin(), selected.end(), [](const Rate &a, const Rate &b) { return a.begin < b.begin; });
    return true;
}

bool compatiblePlan(const TariffPlan &plan) {
    if (plan.origin != TariffOrigin::UserDeclaredOffline || plan.supplier != "Octopus Energy" ||
        !agileProduct(plan.product) || plan.territory.size() != 1 ||
        std::string_view("ABCDEFGHJKLMNP").find(plan.territory) == std::string_view::npos ||
        plan.currency != "GBP" || plan.tiers != TierMode::None ||
        plan.rounding != TariffRounding::MeterHalfHourKwhHalfEven2 ||
        plan.clock.kind != CivilClockKind::NamedIana || plan.clock.zone != "Europe/London" ||
        !plan.cycleCharge.zero() || plan.minimumCycleSubtotal ||
        !plan.subtotalMultiplier || plan.subtotalMultiplier->text() != "1" || plan.revisions.size() != 1)
        return false;
    const auto &revision = plan.revisions.front();
    return revision.timeMode == TimeRateMode::Dynamic && revision.dynamic.empty() && revision.chargePerCivilDay.zero();
}

bool localMidnight(const CivilClock &clock, int64_t instant) {
    const auto date = civilAt(clock, instant);
    return date.status == CivilStatus::Ok && date.millisecondOfDay == 0;
}

}  // namespace

const char *importOctopusRates(const std::string &bytes, int64_t cutoff, TariffDeclaration &declaration,
                              OctopusRateImport &summary, const std::function<bool()> &stop) {
    auto &plan = declaration.plan;
    if (!compatiblePlan(plan)) return "tariff declaration is unsuitable for an Octopus Agile rate import";

    JsonValue root;
    if (!parseJsonStrict(bytes, &root, nullptr, {maxOctopusRateFileBytes, 7}) ||
        !fields(root, {"schema_version", "product_code", "tariff_code", "payment_method", "observed_utc_ms",
                       "unit_rate_pages", "standing_charge_pages"}))
        return "invalid Octopus Agile rate file";
    uint64_t version = 0, observed = 0;
    const auto &product = *root.find("product_code");
    const auto &tariff = *root.find("tariff_code");
    const auto &method = *root.find("payment_method");
    const auto &placeholder = plan.revisions.front();
    if (!root.find("schema_version")->asUint64(&version) || version != 1 ||
        !product.isString() || product.text != plan.product || !tariff.isString() ||
        tariff.text != "E-1R-" + plan.product + "-" + plan.territory || !paymentName(method) ||
        !root.find("observed_utc_ms")->asUint64(&observed) || !observed || observed > meterBillMaxUtcMs)
        return "invalid Octopus Agile rate identity or observation";
    if (cutoff <= 0 || observed > static_cast<uint64_t>(cutoff))
        return "Octopus Agile rates observed after the review cutoff";
    if (placeholder.endUtcMs > static_cast<int64_t>(observed))
        return "Octopus Agile review requires a wholly observed rate window";
    if (!localMidnight(plan.clock, placeholder.beginUtcMs) || !localMidnight(plan.clock, placeholder.endUtcMs))
        return "Octopus Agile rate window must start and end at London local midnight";

    std::vector<Rate> unit, fees;
    if (!rates(*root.find("unit_rate_pages"), method.text, maxUnitRows, false, unit, stop) ||
        !rates(*root.find("standing_charge_pages"), method.text, maxStandingRows, true, fees, stop))
        return "invalid or incomplete Octopus Agile rate pages";
    int64_t cursor = placeholder.beginUtcMs;
    for (const auto &rate : unit) {
        if (rate.begin != cursor || rate.end - rate.begin != halfHourMs || rate.end > placeholder.endUtcMs)
            return "Octopus Agile energy rates must cover every exact half hour once";
        cursor = rate.end;
    }
    if (cursor != placeholder.endUtcMs)
        return "Octopus Agile energy rates must cover every exact half hour once";

    cursor = placeholder.beginUtcMs;
    std::size_t slot = 0;
    std::vector<TariffRevision> revisions;
    const auto published = (std::max)(placeholder.publishedUtcMs, static_cast<int64_t>(observed));
    for (const auto &fee : fees) {
        if (stop && stop()) return "Octopus Agile rate import cancelled";
        const auto begin = (std::max)(placeholder.beginUtcMs, fee.begin);
        const auto end = (std::min)(placeholder.endUtcMs, fee.end);
        if (begin >= end) continue;
        if (cursor != begin || !localMidnight(plan.clock, begin) || !localMidnight(plan.clock, end) ||
            revisions.size() == 128)
            return "Octopus Agile standing charges need complete, unambiguous London days";
        TariffRevision revision;
        revision.key = "octopus-agile-" + std::to_string(revisions.size() + 1);
        revision.beginUtcMs = begin;
        revision.endUtcMs = end;
        revision.publishedUtcMs = published;
        revision.timeMode = TimeRateMode::Dynamic;
        revision.chargePerCivilDay = fee.pounds;
        while (slot < unit.size() && unit[slot].begin < end) {
            const auto &rate = unit[slot++];
            if (rate.end > end) return "Octopus Agile standing charge changes inside a half hour";
            revision.dynamic.push_back({rate.begin, rate.end, published, rate.pounds});
        }
        revisions.push_back(std::move(revision));
        cursor = end;
    }
    if (cursor != placeholder.endUtcMs || slot != unit.size())
        return "Octopus Agile standing charges need complete, unambiguous London days";
    summary = {static_cast<int64_t>(observed), unit.size(), method.text};
    plan.revisions = std::move(revisions);
    return nullptr;
}

}  // namespace om
