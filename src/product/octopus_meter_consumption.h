#pragma once

#include "electricity.h"
#include "octopus_json.h"

#include <functional>
#include <utility>
#include <vector>

namespace om::octopus_detail {

inline const char *decodeConsumption(const JsonValue &rows, int64_t begin, int64_t end,
                                     std::vector<MeterImportInterval> &readings,
                                     const std::function<bool()> &stopped) {
    readings.reserve(rows.items.size());
    for (const auto &row : rows.items) {
        if (stopped()) return "review cancelled";
        MeterImportInterval reading;
        ExactDecimal kwh;
        if (!fields(row, {"consumption", "interval_start", "interval_end"}) ||
            !utcInstant(*row.find("interval_start"), reading.beginUtcMs, true) ||
            !utcInstant(*row.find("interval_end"), reading.endUtcMs, true) ||
            reading.beginUtcMs % halfHourMs || reading.endUtcMs - reading.beginUtcMs != halfHourMs ||
            !exactNumber(*row.find("consumption"), kwh) || kwh.negative() || kwh.scale() > 3)
            return "invalid Octopus half-hour electricity import";
        auto imported = electricityKwh(kwh);
        if (!imported) return "Octopus consumption exceeds arithmetic capacity";
        reading.importedKwh = std::move(*imported);
        readings.push_back(std::move(reading));
    }
    std::sort(readings.begin(), readings.end(), [](const auto &a, const auto &b) {
        return a.beginUtcMs < b.beginUtcMs;
    });
    for (const auto &reading : readings) {
        if (reading.beginUtcMs != begin || reading.endUtcMs > end)
            return "Octopus imports must cover the declared window once, without gaps or overlap";
        begin = reading.endUtcMs;
    }
    return begin == end ? nullptr : "Octopus imports must cover the declared window once, without gaps or overlap";
}

// Links are evidence of pagination and route consistency. They are never requested or retained.
inline std::string_view consumptionMeterRoute(const JsonValue &link) {
    constexpr std::string_view current = "https://api.oegb-kraken.energy";
    constexpr std::string_view legacy = "https://api.octopus.energy";
    constexpr std::string_view prefix = "/v1/electricity-meter-points/", meters = "/meters/", suffix = "/consumption";
    if (!link.isString() || link.text.size() > 4096) return {};
    std::string_view path(link.text);
    if (path.substr(0, current.size()) == current) path.remove_prefix(current.size());
    else if (path.substr(0, legacy.size()) == legacy) path.remove_prefix(legacy.size());
    else return {};
    path = path.substr(0, path.find('?'));
    if (path.empty()) return {};
    if (path.back() == '/') path.remove_suffix(1);
    if (path.substr(0, prefix.size()) != prefix) return {};
    auto route = path.substr(prefix.size());
    constexpr size_t mpanLength = 13;
    const auto serialBegin = mpanLength + meters.size();
    if (route.size() <= serialBegin + suffix.size() || route.substr(mpanLength, meters.size()) != meters ||
        route.substr(route.size() - suffix.size()) != suffix) return {};
    const auto mpan = route.substr(0, mpanLength);
    const auto serial = route.substr(serialBegin, route.size() - serialBegin - suffix.size());
    if (!std::all_of(mpan.begin(), mpan.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
        serial.size() > 128 || serial == "." || serial == ".." ||
        !std::all_of(serial.begin(), serial.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '_' || c == '-' || c == '.';
        })) return {};
    return route;
}

inline bool appendConsumptionPage(const JsonValue &page, size_t index, size_t expected, size_t &pageSize,
                                  std::string &meterRoute, JsonValue &rows) {
    constexpr size_t maxPageRows = 100;
    if (rows.items.size() >= expected || !fields(page, {"count", "next", "previous", "results"})) return false;
    uint64_t count = 0;
    const auto &results = *page.find("results");
    if (!page.find("count")->asUint64(&count) || count != expected || !results.isArray() ||
        results.items.empty() || results.items.size() > maxPageRows)
        return false;
    if (index == 1) pageSize = results.items.size();
    if (results.items.size() != (std::min)(pageSize, expected - rows.items.size())) return false;
    const bool more = rows.items.size() + results.items.size() < expected;
    for (const auto &[name, present] : {std::pair{"next", more}, std::pair{"previous", index > 1}}) {
        const auto &link = *page.find(name);
        if (!present) {
            if (link.type != JsonValue::Type::Null) return false;
            continue;
        }
        const auto route = consumptionMeterRoute(link);
        if (route.empty() || (!meterRoute.empty() && meterRoute != route)) return false;
        meterRoute.assign(route);
    }
    rows.items.insert(rows.items.end(), results.items.begin(), results.items.end());
    return true;
}

}  // namespace om::octopus_detail
