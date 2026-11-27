#include "market_data.h"
#include "json_dom.h"
#include "market_minute_window.h"
#include <algorithm>
#include <set>

namespace om {
const char *marketDataStatusName(MarketDataStatus status) {
    switch (status) {
        case MarketDataStatus::Ok: return "ok";
        case MarketDataStatus::Transport: return "public transport unavailable";
        case MarketDataStatus::Identity: return "unexpected asset/network/market identity";
        case MarketDataStatus::Schema: return "unsupported market response schema";
        case MarketDataStatus::Decimal: return "invalid/out-of-budget financial decimal";
        case MarketDataStatus::Timestamp: return "invalid market observation/minute time";
        case MarketDataStatus::UnsupportedConvention: return "candle time/dialect not production-qualified";
        case MarketDataStatus::Coverage: return "incomplete minute coverage";
        case MarketDataStatus::Arithmetic: return "exact market arithmetic limit exceeded";
        case MarketDataStatus::ProviderError: return "provider error envelope";
        case MarketDataStatus::RateLimited: return "provider rate limit envelope";
    }
    return "unsupported market data";
}
const char *marketSeriesName(MarketSeries series) {
    switch (series) {
        case MarketSeries::GatePearlMainnetUsdtV1: return "Gate international / Pearl mainnet PRL (PEARL_USDT) / USDT per PRL";
        case MarketSeries::KrakenUsdtUsdV1: return "Kraken Spot / USDTZUSD / USD per USDT";
    }
    return "unsupported asset/network/market";
}
namespace {
template<class T> MarketDataResult<T> failure(MarketDataStatus status) { MarketDataResult<T> result; result.status = status; return result; }
bool decimal(const JsonValue &value, ExactDecimal *out, bool positive) {
    if (!value.isString()) return false;
    ExactDecimal n;
    // Provider precision budget, not Pearl atomic precision. Unsupported precision is never silently rounded.
    if (!ExactDecimal::parse(value.text, &n) || n.negative() || n.digits() > 38 || n.scale() > 18 || (positive && n.zero())) return false;
    *out = std::move(n); return true;
}
bool timestamp(const JsonValue &value, bool asString, uint64_t *out) {
    JsonValue token = value;
    if (asString) { if (!value.isString()) return false; token.type = JsonValue::Type::Number; }
    return token.asUint64(out) && *out > 0 && *out <= 253402300799ULL;
}
MarketDataStatus envelope(const PublicJsonResponse &r, const MarketParseContext &c, JsonValue *root, MarketObservation *observation) {
    if (r.status != PublicHttpsStatus::Ok || r.httpStatus != 200) return MarketDataStatus::Transport;
    std::string ignored;
    if (!publicFeedUrl(c.request, &ignored)) return MarketDataStatus::Identity;
    const bool declared=c.origin==MarketOrigin::DeclaredPayload;
    if (!r.observedUtc || r.observedUtc>253402300799ULL || !c.receivedUtc || r.observedUtc>c.receivedUtc ||
        (declared ? (c.requestStartedUtc || c.receivedMonoMs) :
                    (!c.requestStartedUtc || r.observedUtc<c.requestStartedUtc)))
        return MarketDataStatus::Timestamp;
    if (!c.generation) return MarketDataStatus::Identity;
    if (!declared && c.origin!=MarketOrigin::ProviderResponse && c.origin!=MarketOrigin::SyntheticFixture)
        return MarketDataStatus::Schema;
    if (!boundedPublicJson(r.body) || !parseJsonStrict(r.body, root, nullptr)) return MarketDataStatus::Schema;
    observation->series = (c.request.feed == PublicFeed::GatePearlTicker || c.request.feed == PublicFeed::GatePearlCandles) ?
        MarketSeries::GatePearlMainnetUsdtV1 : MarketSeries::KrakenUsdtUsdV1;
    observation->origin = c.origin; observation->requestStartedUtc = c.requestStartedUtc; observation->observedUtc = r.observedUtc;
    observation->receivedMonoMs = c.receivedMonoMs; observation->generation = c.generation;
    return MarketDataStatus::Ok;
}
const JsonValue *krakenResult(const JsonValue &root, MarketDataStatus *status) {
    const auto *error = root.find("error"), *result = root.find("result");
    *status = MarketDataStatus::Schema;
    if (!root.isObject() || !error || !error->isArray()) return nullptr;
    if (!error->items.empty()) {
        bool rate = false;
        for (const auto &message : error->items) {
            if (!message.isString()) return nullptr;
            if (message.text == "EAPI:Rate limit exceeded") rate = true;
        }
        *status = rate ? MarketDataStatus::RateLimited : MarketDataStatus::ProviderError;
        return nullptr;
    }
    if (!result || !result->isObject()) return nullptr;
    *status = MarketDataStatus::Ok;
    return result;
}
bool ohlc(const MarketMinute &m) {
    return m.high.compare(m.low) >= 0 && m.open.compare(m.low) >= 0 && m.open.compare(m.high) <= 0 &&
        m.close.compare(m.low) >= 0 && m.close.compare(m.high) <= 0;
}
bool fixtureConvention(const MarketCandles &candles) {
    return candles.observation.origin == MarketOrigin::SyntheticFixture && candles.convention == MarketMinuteConvention::FixtureMinuteStart;
}
bool validSeries(const MarketCandles &s) {
    if (!fixtureConvention(s) || s.minutes.size() > 1000 || !s.observation.generation || !s.observation.observedUtc ||
        s.observation.observedUtc > 253402300799ULL) return false;
    uint64_t last = 0;
    for (const auto &m : s.minutes) {
        if (!m.startUtc || m.startUtc % 60 || m.startUtc <= last || m.startUtc > s.observation.observedUtc ||
            (m.closed && m.startUtc + 60 > s.observation.observedUtc) || m.baseVolume.negative() || !ohlc(m)) return false;
        for (const auto *price : {&m.open, &m.high, &m.low, &m.close})
            if (price->zero() || price->negative() || price->digits() > 38 || price->scale() > 18) return false;
        last = m.startUtc;
    }
    return true;
}
MarketFreshness freshness(const MarketQuote *q,MarketSeries series,uint64_t utc,
                          std::optional<uint64_t> mono,const MarketFreshnessPolicy &policy) {
    if (!q) return MarketFreshness::Missing;
    if (q->observation.series != series) return MarketFreshness::WrongIdentity;
    const auto &o = q->observation;
    const bool allowedOrigin=mono ? (o.origin==MarketOrigin::ProviderResponse || o.origin==MarketOrigin::SyntheticFixture)
                                  : o.origin==MarketOrigin::DeclaredPayload;
    if (!o.observedUtc || !o.generation || o.observedUtc>utc || utc>253402300799ULL ||
        (mono && (!o.requestStartedUtc || o.requestStartedUtc>o.observedUtc || *mono<o.receivedMonoMs)) ||
        (!mono && (o.requestStartedUtc || o.receivedMonoMs)))
        return MarketFreshness::InvalidClock;
    // Live checks require both clocks. A declared replay uses only the stated UTC times.
    if ((mono && *mono-o.receivedMonoMs>policy.maxObservationAgeMs) || utc-o.observedUtc>policy.maxObservationAgeMs/1000)
        return MarketFreshness::Stale;
    if (!allowedOrigin || q->last.negative() || q->last.zero() || q->last.digits()>38 || q->last.scale()>18)
        return MarketFreshness::WrongIdentity;
    if (policy.requireEventTime && !q->sourceEventUtc) return MarketFreshness::EventTimeUnavailable;
    // No timestamp-bearing adapter is qualified yet. Callers cannot turn an injected event time into policy eligibility.
    if (q->sourceEventUtc) return MarketFreshness::WrongIdentity;
    return MarketFreshness::Available;
}
}  // namespace

MarketDataStatus market_detail::krakenEnvelopeStatus(const JsonValue &payload) {
    MarketDataStatus status;
    krakenResult(payload, &status);
    return status;
}

MarketDataResult<MarketQuote> parseMarketQuote(const PublicJsonResponse &r, const MarketParseContext &c) {
    if (c.request.feed != PublicFeed::GatePearlTicker && c.request.feed != PublicFeed::KrakenUsdtTicker) return failure<MarketQuote>(MarketDataStatus::Identity);
    JsonValue root;
    MarketQuote quote;
    const auto status = envelope(r, c, &root, &quote.observation);
    if (status != MarketDataStatus::Ok) return failure<MarketQuote>(status);
    const JsonValue *last = nullptr, *bid = nullptr, *ask = nullptr;
    if (c.request.feed == PublicFeed::GatePearlTicker) {
        if (!root.isArray() || root.items.size() != 1 || !root.items[0].isObject()) return failure<MarketQuote>(MarketDataStatus::Schema);
        const auto &record = root.items[0];
        const auto *pair = record.find("currency_pair");
        if (!pair || !pair->isString() || pair->text != "PEARL_USDT") return failure<MarketQuote>(MarketDataStatus::Identity);
        last = record.find("last"); bid = record.find("highest_bid"); ask = record.find("lowest_ask");
    } else {
        MarketDataStatus error;
        const auto *result = krakenResult(root, &error);
        if (!result) return failure<MarketQuote>(error);
        if (result->members.size() != 1 || !result->find("USDTZUSD")) return failure<MarketQuote>(MarketDataStatus::Identity);
        const auto &record = *result->find("USDTZUSD");
        if (!record.isObject()) return failure<MarketQuote>(MarketDataStatus::Schema);
        const auto *close = record.find("c"), *a = record.find("a"), *b = record.find("b");
        ExactDecimal volume;
        if (!close || !close->isArray() || close->items.size() != 2 || !decimal(close->items[1], &volume, false)) return failure<MarketQuote>(MarketDataStatus::Schema);
        last = &close->items[0];
        if (a || b) {
            if (!a || !b || !a->isArray() || !b->isArray() || a->items.size() != 3 || b->items.size() != 3) return failure<MarketQuote>(MarketDataStatus::Schema);
            ask = &a->items[0]; bid = &b->items[0];
        }
    }
    if (!last || !decimal(*last, &quote.last, true)) return failure<MarketQuote>(MarketDataStatus::Decimal);
    if (bid || ask) {
        ExactDecimal b, a;
        if (!bid || !ask || !decimal(*bid, &b, true) || !decimal(*ask, &a, true) || b.compare(a) > 0) return failure<MarketQuote>(MarketDataStatus::Decimal);
        quote.bid = b; quote.ask = a;
    }
    MarketDataResult<MarketQuote> result; result.status = MarketDataStatus::Ok; result.value = std::move(quote); return result;
}

MarketDataResult<MarketCandles> parseMarketCandles(const PublicJsonResponse &r, const MarketParseContext &c) {
    if (c.request.feed != PublicFeed::GatePearlCandles && c.request.feed != PublicFeed::KrakenUsdtCandles) return failure<MarketCandles>(MarketDataStatus::Identity);
    if (c.origin != MarketOrigin::SyntheticFixture || c.convention != MarketMinuteConvention::FixtureMinuteStart)
        return failure<MarketCandles>(MarketDataStatus::UnsupportedConvention);
    JsonValue root;
    MarketCandles candles;
    const auto status = envelope(r, c, &root, &candles.observation);
    if (status != MarketDataStatus::Ok) return failure<MarketCandles>(status);
    candles.convention = c.convention;
    const bool gate = c.request.feed == PublicFeed::GatePearlCandles;
    const JsonValue *rows = &root;
    if (!gate) {
        MarketDataStatus error;
        const auto *result = krakenResult(root, &error);
        if (!result) return failure<MarketCandles>(error);
        uint64_t cursor = 0;
        if (!result || !result->find("last") || !result->find("last")->asUint64(&cursor)) return failure<MarketCandles>(MarketDataStatus::Schema);
        if (result->members.size() != 2 || !result->find("USDTZUSD")) return failure<MarketCandles>(MarketDataStatus::Identity);
        candles.committedCursor = cursor; rows = result->find("USDTZUSD");
    }
    if (!rows->isArray() || rows->items.size() > (gate ? 1000u : 720u) || (!gate && rows->items.empty())) return failure<MarketCandles>(MarketDataStatus::Schema);
    std::set<uint64_t> seen;
    for (size_t index = 0; index < rows->items.size(); ++index) {
        const auto &row = rows->items[index];
        if (!row.isArray() || row.items.size() != 8) return failure<MarketCandles>(MarketDataStatus::Schema);
        const auto &v = row.items;
        MarketMinute minute;
        if (!timestamp(v[0], gate, &minute.startUtc) || minute.startUtc % 60 || minute.startUtc > r.observedUtc || !seen.insert(minute.startUtc).second)
            return failure<MarketCandles>(MarketDataStatus::Timestamp);
        if (gate) {
            if (!v[7].isString() || (v[7].text != "true" && v[7].text != "false")) return failure<MarketCandles>(MarketDataStatus::Schema);
            ExactDecimal quoteVolume;
            if (!decimal(v[1], &quoteVolume, false) || !decimal(v[2], &minute.close, true) || !decimal(v[3], &minute.high, true) ||
                !decimal(v[4], &minute.low, true) || !decimal(v[5], &minute.open, true) || !decimal(v[6], &minute.baseVolume, false))
                return failure<MarketCandles>(MarketDataStatus::Decimal);
            minute.closed = v[7].text == "true";
        } else {
            ExactDecimal vwap;
            uint64_t count = 0;
            if (!decimal(v[1], &minute.open, true) || !decimal(v[2], &minute.high, true) || !decimal(v[3], &minute.low, true) ||
                !decimal(v[4], &minute.close, true) || !decimal(v[5], &vwap, true) || !decimal(v[6], &minute.baseVolume, false) || !v[7].asUint64(&count))
                return failure<MarketCandles>(MarketDataStatus::Decimal);
            minute.closed = index + 1 != rows->items.size();
        }
        if (!ohlc(minute)) return failure<MarketCandles>(MarketDataStatus::Decimal);
        if (minute.closed && minute.startUtc + 60 > r.observedUtc) return failure<MarketCandles>(MarketDataStatus::Timestamp);
        if (!gate && !minute.closed) continue;  // exclude original terminal row BEFORE sorting, even if its time looks old
        candles.minutes.push_back(std::move(minute));
    }
    std::sort(candles.minutes.begin(), candles.minutes.end(), [](const auto &a, const auto &b) { return a.startUtc < b.startUtc; });
    MarketDataResult<MarketCandles> result; result.status = MarketDataStatus::Ok; result.value = std::move(candles); return result;
}

MarketWindow marketWindow(const MarketCandles &pearl, const MarketCandles *fx, uint64_t start, uint64_t end) {
    auto result = market_detail::minuteWindowBounds(start, end);
    if (!result.expected) return result;
    if (pearl.observation.series != MarketSeries::GatePearlMainnetUsdtV1 || !validSeries(pearl) || end > pearl.observation.observedUtc) { result.status = MarketDataStatus::Identity; return result; }
    const bool fxEligible = fx && fx->observation.series == MarketSeries::KrakenUsdtUsdV1 && validSeries(*fx) && end <= fx->observation.observedUtc &&
        fx->observation.generation == pearl.observation.generation && fx->observation.origin == pearl.observation.origin;
    return market_detail::accumulateMinuteWindow(result, pearl.minutes, fxEligible ? &fx->minutes : nullptr);
}

static MarketLatest referenceAt(const MarketQuote *pearl,const MarketQuote *fx,uint64_t utc,
                                std::optional<uint64_t> mono,const MarketFreshnessPolicy &policy) {
    MarketLatest result;
    result.tokenStatus = freshness(pearl, MarketSeries::GatePearlMainnetUsdtV1, utc, mono, policy);
    const auto fxStatus = freshness(fx, MarketSeries::KrakenUsdtUsdV1, utc, mono, policy);
    const auto age=[&](const MarketQuote *quote)->std::optional<uint64_t> {
        if (!quote) return {};
        const auto &o=quote->observation;
        if (mono) return *mono>=o.receivedMonoMs ? std::optional<uint64_t>(*mono-o.receivedMonoMs) : std::nullopt;
        if (o.observedUtc && utc>=o.observedUtc && utc<=253402300799ULL) return (utc-o.observedUtc)*1000;
        return {};
    };
    result.tokenObservationAgeMs=age(pearl);
    result.fxObservationAgeMs=age(fx);
    result.usdStatus = result.tokenStatus != MarketFreshness::Available ? result.tokenStatus : fxStatus;
    if (result.tokenStatus == MarketFreshness::Available) result.tokenUsdt = pearl->last;
    if (fxStatus == MarketFreshness::Available) result.usdtUsd = fx->last;
    if (result.usdStatus != MarketFreshness::Available) return result;
    const auto &p = pearl->observation, &f = fx->observation;
    const auto gap = p.receivedMonoMs > f.receivedMonoMs ? p.receivedMonoMs - f.receivedMonoMs : f.receivedMonoMs - p.receivedMonoMs;
    const auto utcGap = p.observedUtc > f.observedUtc ? p.observedUtc - f.observedUtc : f.observedUtc - p.observedUtc;
    if (p.generation != f.generation || p.origin != f.origin) { result.usdStatus = MarketFreshness::WrongIdentity; return result; }
    if ((mono && gap>policy.maxCrossFeedSkewMs) || utcGap>policy.maxCrossFeedSkewMs/1000) { result.usdStatus = MarketFreshness::Skew; return result; }
    ExactDecimal product;
    if (!pearl->last.multiply(fx->last, &product)) { result.usdStatus = MarketFreshness::WrongIdentity; return result; }
    result.tokenUsd = product; return result;
}

MarketLatest latestMarketReference(const MarketQuote *pearl,const MarketQuote *fx,uint64_t utc,
                                  uint64_t mono,MarketFreshnessPolicy policy) {
    return referenceAt(pearl,fx,utc,mono,policy);
}
MarketLatest declaredMarketReference(const MarketQuote *pearl,const MarketQuote *fx,uint64_t asOfUtc) {
    return referenceAt(pearl,fx,asOfUtc,std::nullopt,declaredTickerV1Policy);
}
}  // namespace om
