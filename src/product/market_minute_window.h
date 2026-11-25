#pragma once
#include "market_data.h"

namespace om::market_detail {

inline MarketWindow minuteWindowBounds(uint64_t start, uint64_t end) {
    MarketWindow result;
    result.startUtc = start;
    result.endUtc = end;
    if (start && start % 60 == 0 && end % 60 == 0 && end > start &&
        end - start <= 719 * 60 && end <= 253402300799ULL)
        result.expected = static_cast<unsigned>((end - start) / 60);
    return result;
}

// Input admission belongs to the caller. Both series are ordered, unique and already validated.
template<class Minute>
MarketWindow accumulateMinuteWindow(MarketWindow result, const std::vector<Minute> &pearl,
                                    const std::vector<Minute> *fx) {
    ExactDecimal usdtSum, usdSum;
    size_t p = 0, f = 0;
    for (uint64_t t = result.startUtc; t < result.endUtc; t += 60) {
        while (p < pearl.size() && pearl[p].startUtc < t) ++p;
        while (fx && f < fx->size() && (*fx)[f].startUtc < t) ++f;
        const auto *minute = p < pearl.size() && pearl[p].startUtc == t && pearl[p].closed ? &pearl[p] : nullptr;
        if (!minute) continue;
        ++result.pearlCovered;
        if (!usdtSum.add(minute->close, &usdtSum)) { result.status = MarketDataStatus::Arithmetic; return result; }
        if (fx && f < fx->size() && (*fx)[f].startUtc == t && (*fx)[f].closed) {
            ExactDecimal product;
            if (!minute->close.multiply((*fx)[f].close, &product) || !usdSum.add(product, &usdSum)) { result.status = MarketDataStatus::Arithmetic; return result; }
            ++result.fxCovered;
        }
    }
    if (result.pearlCovered != result.expected) return result;
    result.tokenUsdt = MarketExactMean{usdtSum, result.expected};
    result.status = MarketDataStatus::Ok;
    if (result.fxCovered == result.expected) result.tokenUsd = MarketExactMean{usdSum, result.expected};
    return result;
}

}  // namespace om::market_detail
