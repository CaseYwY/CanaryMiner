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

}  // namespace om::octopus_detail
