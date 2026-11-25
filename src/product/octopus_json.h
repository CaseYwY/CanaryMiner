#pragma once

#include "civil_time.h"
#include "exact_decimal.h"
#include "json_dom.h"
#include "meter_bill.h"

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <string_view>

namespace om::octopus_detail {

inline constexpr int64_t halfHourMs = 1800000;

inline bool fields(const JsonValue &value, std::initializer_list<const char *> names) {
    return value.isObject() && value.members.size() == names.size() &&
        std::all_of(names.begin(), names.end(), [&](const char *name) { return value.find(name) != nullptr; });
}

inline unsigned digits(std::string_view value) {
    unsigned result = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return 10000;
        result = result * 10 + static_cast<unsigned>(c - '0');
    }
    return result;
}

// The consumption API may express the same UTC boundary with its London offset.
inline bool utcInstant(const JsonValue &value, int64_t &instant, bool allowOffset = false) {
    if (!value.isString()) return false;
    const std::string_view s = value.text;
    const bool utc = s.size() == 20 && s[19] == 'Z';
    if ((!utc && (!allowOffset || s.size() != 25 || (s[19] != '+' && s[19] != '-') || s[22] != ':')) ||
        s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':')
        return false;
    const auto day = civilSerialDay({static_cast<int>(digits(s.substr(0, 4))), digits(s.substr(5, 2)),
                                     digits(s.substr(8, 2))});
    const auto hour = digits(s.substr(11, 2)), minute = digits(s.substr(14, 2)), second = digits(s.substr(17, 2));
    if (!day || hour >= 24 || minute >= 60 || second >= 60) return false;
    int64_t seconds = *day * 86400 + hour * 3600 + minute * 60 + second;
    if (!utc) {
        const auto offsetHour = digits(s.substr(20, 2)), offsetMinute = digits(s.substr(23, 2));
        if (offsetHour > 14 || offsetMinute >= 60 || (offsetHour == 14 && offsetMinute))
            return false;
        const auto offsetSeconds = static_cast<int64_t>(offsetHour * 3600 + offsetMinute * 60);
        seconds += s[19] == '+' ? -offsetSeconds : offsetSeconds;
    }
    if (seconds < 0 || seconds > meterBillMaxUtcMs / 1000) return false;
    instant = seconds * 1000;
    return true;
}

inline bool exactNumber(const JsonValue &value, ExactDecimal &amount) {
    const std::string &token = value.text;
    if (!value.isNumber() || token.size() > ExactDecimal::maxDigits + ExactDecimal::maxScale + 7)
        return false;
    const auto power = token.find_first_of("eE");
    if (power == std::string::npos)
        return ExactDecimal::parse(token, &amount) && amount.digits() <= 38 && amount.scale() <= 18;

    const auto *exponentStart = token.data() + power + 1;
    const auto *end = token.data() + token.size();
    if (*exponentStart == '+') ++exponentStart;
    int exponent = 0;
    const auto parsed = std::from_chars(exponentStart, end, exponent);
    constexpr int exponentLimit = ExactDecimal::maxDigits + ExactDecimal::maxScale;
    if (parsed.ec != std::errc{} || parsed.ptr != end || exponent < -exponentLimit || exponent > exponentLimit)
        return false;

    const bool negative = token.front() == '-';
    std::string coefficient = token.substr(negative, power - negative);
    int scale = -exponent;
    const auto point = coefficient.find('.');
    if (point != std::string::npos) {
        scale += static_cast<int>(coefficient.size() - point - 1);
        coefficient.erase(point, 1);
    }
    const auto first = coefficient.find_first_not_of('0');
    if (first == std::string::npos) {
        amount = ExactDecimal{};
        return true;
    }
    coefficient.erase(0, first);
    while (scale > 0 && coefficient.back() == '0') {
        coefficient.pop_back();
        --scale;
    }
    if (scale > 18 || coefficient.size() + static_cast<std::size_t>((std::max)(0, -scale)) > 38)
        return false;
    if (scale < 0) {
        coefficient.append(static_cast<std::size_t>(-scale), '0');
    } else if (scale > 0) {
        const auto places = static_cast<std::size_t>(scale);
        if (coefficient.size() <= places) coefficient.insert(0, places + 1 - coefficient.size(), '0');
        coefficient.insert(coefficient.size() - places, 1, '.');
    }
    return ExactDecimal::parse(negative ? "-" + coefficient : coefficient, &amount);
}

}  // namespace om::octopus_detail
