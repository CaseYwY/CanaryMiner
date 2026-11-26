#include "market_ticker_review.h"
#include "market_trade_data.h"
#include "json_dom.h"
#include "market_public_sample.h"

#include <charconv>
#include <set>

namespace om {
namespace {

constexpr uint64_t maxAgeMs=120000, maxSkewMs=30000;

struct Trade {
    MarketTradeTime time;
    ExactDecimal price;
    uint64_t id=0;
};

bool unsignedNumber(std::string_view text,uint64_t &out) {
    if (text.empty() || text.size()>20 || (text.size()>1 && text.front()=='0')) return false;
    const auto end=text.data()+text.size();
    const auto parsed=std::from_chars(text.data(),end,out);
    return parsed.ptr==end && parsed.ec==std::errc();
}

bool tradeTime(const JsonValue *value,bool gate,MarketTradeTime &out) {
    if (!value || (gate ? !value->isString() : value->type!=JsonValue::Type::Number)) return false;
    const std::string_view text(value->text);
    const auto dot=text.find('.');
    uint64_t whole=0, fraction=0;
    if (!unsignedNumber(text.substr(0,dot),whole) || !whole ||
        whole>(gate ? market_detail::maxMarketUtcMs : market_detail::maxMarketUtcMs/1000)) return false;
    if (dot!=std::string_view::npos) {
        const auto tail=text.substr(dot+1);
        const size_t precision=gate ? 6 : 9;
        if (tail.empty() || tail.size()>precision || tail.find_first_not_of("0123456789")!=std::string_view::npos)
            return false;
        for (const char digit:tail) fraction=fraction*10+static_cast<unsigned>(digit-'0');
        for (size_t i=tail.size();i<precision;++i) fraction*=10;
    }
    out=gate ? MarketTradeTime{whole,static_cast<uint32_t>(fraction)} :
               MarketTradeTime{whole*1000+fraction/1000000,static_cast<uint32_t>(fraction%1000000)};
    return true;
}

bool positivePrice(const JsonValue *value,ExactDecimal &out) {
    return value && value->isString() && ExactDecimal::parse(value->text,&out) &&
        !out.negative() && !out.zero() && out.digits()<=38 && out.scale()<=18;
}

MarketDataStatus gateRow(const JsonValue &row,Trade &out) {
    if (!row.isObject()) return MarketDataStatus::Schema;
    const auto *pair=row.find("currency_pair"), *quote=row.find("trade_quote");
    if (!pair || !pair->isString() || pair->text!="PEARL_USDT" ||
        (quote && (!quote->isString() || quote->text!="USDT"))) return MarketDataStatus::Identity;
    const auto *id=row.find("id");
    if (!id || !id->isString() || !unsignedNumber(id->text,out.id) || !out.id) return MarketDataStatus::Schema;
    if (!tradeTime(row.find("create_time_ms"),true,out.time)) return MarketDataStatus::Timestamp;
    if (const auto *coarse=row.find("create_time")) {
        uint64_t seconds=0;
        if (!coarse->isString() || !unsignedNumber(coarse->text,seconds) || seconds!=out.time.first/1000)
            return MarketDataStatus::Timestamp;
    }
    return positivePrice(row.find("price"),out.price) ? MarketDataStatus::Ok : MarketDataStatus::Decimal;
}

MarketDataStatus krakenRow(const JsonValue &row,Trade &out) {
    if (!row.isArray() || row.items.size()!=7 || !row.items[6].asUint64(&out.id) || !out.id)
        return MarketDataStatus::Schema;
    if (!tradeTime(&row.items[2],false,out.time)) return MarketDataStatus::Timestamp;
    return positivePrice(&row.items[0],out.price) ? MarketDataStatus::Ok : MarketDataStatus::Decimal;
}

MarketDataResult<Trade> latestTrade(const PublicJsonResponse &response,bool gate,uint64_t sent,uint64_t received,
                                    std::optional<MarketTradeTime> *conflictingEvent) {
    if (response.status!=PublicHttpsStatus::Ok || response.httpStatus!=200)
        return {MarketDataStatus::Transport,{}};
    if (!sent || received<sent || received>market_detail::maxMarketUtcMs ||
        !response.observedUtc || response.observedUtc<sent/1000 || response.observedUtc>received/1000)
        return {MarketDataStatus::Timestamp,{}};
    JsonValue document;
    if (!boundedPublicJson(response.body) || !parseJsonStrict(response.body,&document,nullptr,{maxPublicTradeBodyBytes}))
        return {MarketDataStatus::Schema,{}};

    const JsonValue *rows=&document;
    if (!gate) {
        const auto *errors=document.find("error"), *result=document.find("result");
        if (!document.isObject() || !errors || !errors->isArray()) return {MarketDataStatus::Schema,{}};
        if (!errors->items.empty()) {
            bool limited=false;
            for (const auto &error:errors->items) {
                if (!error.isString()) return {MarketDataStatus::Schema,{}};
                limited=limited || error.text=="EAPI:Rate limit exceeded";
            }
            return {limited ? MarketDataStatus::RateLimited : MarketDataStatus::ProviderError,{}};
        }
        if (!result || !result->isObject()) return {MarketDataStatus::Schema,{}};
        rows=result->find("USDTZUSD");
        const auto *cursor=result->find("last");
        if (!rows || result->members.size()!=2) return {MarketDataStatus::Identity,{}};
        if (!cursor || !cursor->isString() || cursor->text.empty() || cursor->text.size()>20 ||
            cursor->text.find_first_not_of("0123456789")!=std::string::npos) return {MarketDataStatus::Schema,{}};
    }
    if (!rows->isArray() || rows->items.size()>maxPublicTradeRows) return {MarketDataStatus::Schema,{}};
    if (rows->items.empty()) return {MarketDataStatus::Coverage,{}};

    Trade latest;
    bool conflict=false;
    std::set<uint64_t> ids;
    for (const auto &row:rows->items) {
        Trade candidate;
        const auto status=gate ? gateRow(row,candidate) : krakenRow(row,candidate);
        if (status!=MarketDataStatus::Ok) return {status,{}};
        if (candidate.time>MarketTradeTime{received,0} || candidate.time.first/1000>response.observedUtc)
            return {MarketDataStatus::Timestamp,{}};
        if (!ids.insert(candidate.id).second) return {MarketDataStatus::Schema,{}};
        if (candidate.time>latest.time) {
            latest=std::move(candidate);
            conflict=false;
        } else if (candidate.time==latest.time && candidate.price.compare(latest.price)!=0) {
            conflict=true;
        }
    }
    if (conflict) {
        if (conflictingEvent) *conflictingEvent=latest.time;
        return {MarketDataStatus::Coverage,{}};
    }
    return {MarketDataStatus::Ok,std::move(latest)};
}

uint64_t tradeSkewMs(const MarketTradeTime &first,const MarketTradeTime &second) {
    const auto &earlier=(std::min)(first,second), &later=(std::max)(first,second);
    return later.first-earlier.first+static_cast<unsigned>(later.second>earlier.second);
}

std::string eventUtc(const MarketTradeTime &time) {
    const auto nanos=(time.first%1000)*1000000+time.second;
    auto subsecond=std::to_string(nanos);
    subsecond.insert(0,9-subsecond.size(),'0');
    return std::to_string(time.first/1000)+"."+subsecond;
}

std::optional<MarketTradeAges> tradeAges(const MarketTradeObservation &trade,uint64_t utcMs,uint64_t monoMs,TradeEventAge eventAge) {
    if (!utcMs || utcMs>market_detail::maxMarketUtcMs || utcMs<trade.receivedUtcMs ||
        monoMs<trade.receivedMonoMs || trade.time>MarketTradeTime{trade.receivedUtcMs,0}) return {};
    const auto receiptAge=(std::max)(utcMs-trade.receivedUtcMs,monoMs-trade.receivedMonoMs);
    const auto ageAtReceipt=trade.receivedUtcMs-trade.time.first;
    const auto elapsed=eventAge==TradeEventAge::WallAndMonotonic ? receiptAge : utcMs-trade.receivedUtcMs;
    return MarketTradeAges{ageAtReceipt+(std::min)(elapsed,UINT64_MAX-ageAtReceipt),receiptAge};
}

bool tradeFresh(const MarketTradeObservation *trade,uint64_t utcMs,uint64_t monoMs,TradeEventAge eventAge) {
    const auto age=trade ? tradeAges(*trade,utcMs,monoMs,eventAge) : std::nullopt;
    return age && age->eventMs<=maxAgeMs && age->receiptMs<=maxAgeMs;
}

uint64_t gap(uint64_t a,uint64_t b) { return (std::max)(a,b)-(std::min)(a,b); }

void appendTrade(std::string &report,const char *name,const MarketDataResult<MarketTradeObservation> &trade,
                 PublicHttpsStatus transport,const PublicTickerCapture &capture) {
    report+=std::string(name)+": ";
    if (!trade.value) {
        report+="N/A ("+std::string(marketTradeStatusName(trade.status,transport))+")\n";
        return;
    }
    const auto &value=*trade.value;
    const auto ages=tradeAges(value,capture.cutoffUtc,capture.elapsedMs,TradeEventAge::Wall);
    report+=value.price.text()+" | event UTC "+eventUtc(value.time)+
        " | receipt UTC ms "+std::to_string(value.receivedUtcMs);
    if (ages) {
        report+=" | event age "+std::to_string(ages->eventMs)+" ms | receipt age "+std::to_string(ages->receiptMs)+" ms";
        if (ages->eventMs>maxAgeMs || ages->receiptMs>maxAgeMs) report+=" | outside age limit";
    }
    report+='\n';
}

} // namespace

MarketDataResult<MarketTradeObservation> parsePublicTrade(const PublicJsonResponse &response,PublicFeed feed,
    uint64_t sentUtcMs,uint64_t receivedUtcMs,uint64_t receivedMonoMs,std::optional<MarketTradeTime> *conflictingEvent) {
    if (conflictingEvent) conflictingEvent->reset();
    const bool gate=feed==PublicFeed::GatePearlTrades;
    if (!gate && feed!=PublicFeed::KrakenUsdtTrades) return {MarketDataStatus::Identity,{}};
    auto trade=latestTrade(response,gate,sentUtcMs,receivedUtcMs,conflictingEvent);
    if (!trade.value) return {trade.status,{}};
    return {MarketDataStatus::Ok,
            MarketTradeObservation{trade.value->time,std::move(trade.value->price),receivedUtcMs,receivedMonoMs}};
}

MarketTradeReference publicTradeReference(const MarketTradeObservation *pearl,const MarketTradeObservation *fx,
    uint64_t nowUtcMs,uint64_t nowMonoMs,TradeEventAge eventAge) {
    MarketTradeReference result;
    result.pearlFresh=tradeFresh(pearl,nowUtcMs,nowMonoMs,eventAge);
    result.fxFresh=tradeFresh(fx,nowUtcMs,nowMonoMs,eventAge);
    if (!result.pearlFresh || !result.fxFresh) return result;
    if (tradeSkewMs(pearl->time,fx->time)>maxSkewMs ||
        gap(pearl->receivedUtcMs,fx->receivedUtcMs)>maxSkewMs || gap(pearl->receivedMonoMs,fx->receivedMonoMs)>maxSkewMs) {
        result.unavailable="trade events or receipts more than 30 s apart";
    } else {
        ExactDecimal usd;
        if (pearl->price.multiply(fx->price,&usd)) result.usd=std::move(usd);
        else result.unavailable="combined price exceeds arithmetic capacity";
    }
    return result;
}

std::string marketTradeUtc(const MarketTradeTime &time) { return eventUtc(time); }

std::optional<MarketTradeAges> marketTradeAges(const MarketTradeObservation &trade,uint64_t nowUtcMs,uint64_t nowMonoMs,TradeEventAge eventAge) {
    return tradeAges(trade,nowUtcMs,nowMonoMs,eventAge);
}

const char *marketTradeStatusName(MarketDataStatus status,PublicHttpsStatus transport) {
    if (status==MarketDataStatus::Transport)
        return transport==PublicHttpsStatus::Ok ? "public provider HTTP error" : publicHttpsStatusName(transport);
    if (status==MarketDataStatus::Timestamp) return "invalid trade or receipt timestamp";
    if (status==MarketDataStatus::Coverage) return "no unambiguous trade coverage";
    return marketDataStatusName(status);
}

PublicTradeReport renderPublicTradeSample(const PublicTickerCapture &capture,std::string &report) {
    using market_detail::MarketSampleClock;
    using market_detail::marketTradeClockContinuous;
    const auto &gate=capture.gate,&kraken=capture.kraken;
    const MarketSampleClock start{0,capture.startedUtc}, first{gate.elapsedMs,gate.receivedUtc},
        second{kraken.elapsedMs,kraken.receivedUtc}, cutoff{capture.elapsedMs,capture.cutoffUtc};
    if (!capture.trades || capture.elapsedMs>maxAgeMs || gate.sentUtc<capture.startedUtc ||
        gate.sentUtc>gate.receivedUtc || kraken.sentUtc<gate.receivedUtc || kraken.sentUtc>kraken.receivedUtc ||
        !marketTradeClockContinuous(start,start,first) || !marketTradeClockContinuous(start,first,second) ||
        !marketTradeClockContinuous(start,second,cutoff))
        return PublicTradeReport::InvalidClock;

    const auto pearl=parsePublicTrade(gate.response,PublicFeed::GatePearlTrades,gate.sentUtc,gate.receivedUtc,gate.elapsedMs);
    const auto fx=parsePublicTrade(kraken.response,PublicFeed::KrakenUsdtTrades,kraken.sentUtc,kraken.receivedUtc,kraken.elapsedMs);
    report="Public trade sample (newest timestamp in each returned batch)\n"
        "Capture UTC ms "+std::to_string(capture.cutoffUtc)+" | ages at capture\n";
    appendTrade(report,"Gate Pearl mainnet PRL/USDT",pearl,gate.response.status,capture);
    appendTrade(report,"Kraken USDT/USD",fx,kraken.response.status,capture);
    const auto reference=publicTradeReference(pearl.value?&*pearl.value:nullptr,fx.value?&*fx.value:nullptr,
                                               capture.cutoffUtc,capture.elapsedMs);
    report+="PRL/USD trade reference at capture: ";
    if (reference.usd) {
        report+=reference.usd->text()+'\n';
        return PublicTradeReport::Complete;
    }
    report+="N/A ("+std::string(reference.unavailable)+")\n";
    return PublicTradeReport::Incomplete;
}
} // namespace om
