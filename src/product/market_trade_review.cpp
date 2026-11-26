#include "market_ticker_review.h"
#include "json_dom.h"
#include "market_public_sample.h"

#include <charconv>
#include <set>

namespace om {
namespace {

using TradeTime = std::pair<uint64_t, uint32_t>;  // UTC milliseconds and nanoseconds within that millisecond
constexpr uint64_t maxAgeMs=120000, maxSkewMs=30000;

struct Trade {
    TradeTime time;
    ExactDecimal price;
    uint64_t id=0;
};

bool unsignedNumber(std::string_view text,uint64_t &out) {
    if (text.empty() || text.size()>20 || (text.size()>1 && text.front()=='0')) return false;
    const auto end=text.data()+text.size();
    const auto parsed=std::from_chars(text.data(),end,out);
    return parsed.ptr==end && parsed.ec==std::errc();
}

bool tradeTime(const JsonValue *value,bool gate,TradeTime &out) {
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
    out=gate ? TradeTime{whole,static_cast<uint32_t>(fraction)} :
               TradeTime{whole*1000+fraction/1000000,static_cast<uint32_t>(fraction%1000000)};
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

MarketDataResult<Trade> latestTrade(const PublicTickerReceipt &receipt,bool gate) {
    const auto &response=receipt.response;
    if (response.status!=PublicHttpsStatus::Ok || response.httpStatus!=200)
        return {MarketDataStatus::Transport,{}};
    if (!response.observedUtc || response.observedUtc<receipt.sentUtc/1000 ||
        response.observedUtc>receipt.receivedUtc/1000) return {MarketDataStatus::Timestamp,{}};
    JsonValue document;
    if (!boundedPublicJson(response.body) || !parseJsonStrict(response.body,&document,nullptr,{maxPublicTradeBodyBytes}))
        return {MarketDataStatus::Schema,{}};

    const JsonValue *rows=&document;
    if (!gate) {
        const auto *errors=document.find("error"), *result=document.find("result");
        if (!document.isObject() || !errors || !errors->isArray()) return {MarketDataStatus::Schema,{}};
        if (!errors->items.empty()) return {MarketDataStatus::ProviderError,{}};
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
        if (candidate.time>TradeTime{receipt.receivedUtc,0} || candidate.time.first/1000>response.observedUtc)
            return {MarketDataStatus::Timestamp,{}};
        if (!ids.insert(candidate.id).second) return {MarketDataStatus::Schema,{}};
        if (candidate.time>latest.time) {
            latest=std::move(candidate);
            conflict=false;
        } else if (candidate.time==latest.time && candidate.price.compare(latest.price)!=0) {
            conflict=true;
        }
    }
    if (conflict) return {MarketDataStatus::Coverage,{}};
    return {MarketDataStatus::Ok,std::move(latest)};
}

uint64_t tradeSkewMs(const TradeTime &first,const TradeTime &second) {
    const auto &earlier=(std::min)(first,second), &later=(std::max)(first,second);
    return later.first-earlier.first+static_cast<unsigned>(later.second>earlier.second);
}

std::string eventUtc(const TradeTime &time) {
    const auto nanos=(time.first%1000)*1000000+time.second;
    auto subsecond=std::to_string(nanos);
    subsecond.insert(0,9-subsecond.size(),'0');
    return std::to_string(time.first/1000)+"."+subsecond;
}

bool appendTrade(std::string &report,const char *name,const MarketDataResult<Trade> &trade,
                 const PublicTickerReceipt &receipt,const PublicTickerCapture &capture) {
    report+=std::string(name)+": ";
    if (!trade.value) {
        const char *reason=marketDataStatusName(trade.status);
        if (trade.status==MarketDataStatus::Transport)
            reason=receipt.response.status==PublicHttpsStatus::Ok ? "public provider HTTP error" :
                                                                  publicHttpsStatusName(receipt.response.status);
        else if (trade.status==MarketDataStatus::Timestamp) reason="invalid trade or receipt timestamp";
        else if (trade.status==MarketDataStatus::Coverage) reason="no unambiguous trade coverage";
        report+="N/A ("+std::string(reason)+")\n";
        return false;
    }
    const auto eventAge=capture.cutoffUtc-trade.value->time.first;
    const auto receiveAge=(std::max)(capture.cutoffUtc-receipt.receivedUtc,capture.elapsedMs-receipt.elapsedMs);
    const bool fresh=eventAge<=maxAgeMs && receiveAge<=maxAgeMs;
    report+=trade.value->price.text()+" | event UTC "+eventUtc(trade.value->time)+
        " | receipt UTC ms "+std::to_string(receipt.receivedUtc)+
        " | event age "+std::to_string(eventAge)+" ms | receipt age "+std::to_string(receiveAge)+" ms";
    if (!fresh) report+=" | outside age limit";
    report+='\n';
    return fresh;
}

} // namespace

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

    const auto pearl=latestTrade(gate,true), fx=latestTrade(kraken,false);
    report="Public trade sample (newest timestamp in each returned batch)\n"
        "Capture UTC ms "+std::to_string(capture.cutoffUtc)+" | ages at capture\n";
    const bool pearlFresh=appendTrade(report,"Gate Pearl mainnet PRL/USDT",pearl,gate,capture);
    const bool fxFresh=appendTrade(report,"Kraken USDT/USD",fx,kraken,capture);
    report+="PRL/USD trade reference at capture: ";
    ExactDecimal usd;
    if (!pearlFresh || !fxFresh) {
        report+="N/A (requires two valid trades and receipts within 120 s)\n";
    } else if (tradeSkewMs(pearl.value->time,fx.value->time)>maxSkewMs ||
               kraken.receivedUtc-gate.receivedUtc>maxSkewMs || kraken.elapsedMs-gate.elapsedMs>maxSkewMs) {
        report+="N/A (trade events or receipts more than 30 s apart)\n";
    } else if (!pearl.value->price.multiply(fx.value->price,&usd)) {
        report+="N/A (combined price exceeds arithmetic capacity)\n";
    } else {
        report+=usd.text()+'\n';
        return PublicTradeReport::Complete;
    }
    return PublicTradeReport::Incomplete;
}
} // namespace om
