// Provider-shaped but entirely synthetic fixtures. No price/earnings request, account, wallet or miner.
#include "product/market_data.h"
#include "product/json_dom.h"
#include "product_test_util.h"
#include <algorithm>

using namespace om;
constexpr uint64_t E = 1800000000;
std::string array(const std::vector<std::string> &rows) {
    std::string text = "[";
    for (const auto &row : rows) { if (text.size() > 1) text += ','; text += row; }
    return text + ']';
}
std::string gate(uint64_t t, const std::string &close = "2", const std::string &closed = "true") {
    return "[" + jsonQuote(std::to_string(t)) + ",\"0\"," + jsonQuote(close) + ",\"10\",\"0.1\"," + jsonQuote(close) + ",\"0\"," + jsonQuote(closed) + "]";
}
std::string kraken(uint64_t t, const std::string &close = "1") {
    return "[" + std::to_string(t) + ',' + jsonQuote(close) + ",\"10\",\"0.1\"," + jsonQuote(close) + ',' + jsonQuote(close) + ",\"0\",0]";
}
std::string krakenBody(const std::vector<std::string> &rows) { return "{\"error\":[],\"result\":{\"USDTZUSD\":" + array(rows) + ",\"last\":1800000000}}"; }
PublicJsonResponse response(std::string body) { PublicJsonResponse r; r.status = PublicHttpsStatus::Ok; r.httpStatus = 200; r.observedUtc = E + 10; r.body = std::move(body); return r; }
MarketParseContext context(PublicFeed feed) {
    MarketParseContext c;
    c.request = {feed, (feed == PublicFeed::GatePearlCandles || feed == PublicFeed::KrakenUsdtCandles) ? static_cast<int64_t>(E - 480 * 60) : 0,
        (feed == PublicFeed::GatePearlCandles || feed == PublicFeed::KrakenUsdtCandles) ? static_cast<int64_t>(E) : 0};
    c.requestStartedUtc = E + 9; c.receivedMonoMs = 100000; c.generation = 7;
    c.receivedUtc = E + 10;
    c.origin = MarketOrigin::SyntheticFixture; c.convention = MarketMinuteConvention::FixtureMinuteStart;
    return c;
}
std::string mean(const std::optional<MarketExactMean> &value) {
    CHECK(value.has_value());
    std::string text;
    if (value) CHECK(value->sum.formatRatio(value->count, 6, &text));
    return text;
}
int main() {
    auto gc = context(PublicFeed::GatePearlCandles), kc = context(PublicFeed::KrakenUsdtCandles);
    std::vector<std::string> pr, fr;
    for (unsigned i = 0; i < 480; ++i) { pr.push_back(gate(E - 480 * 60 + i * 60)); fr.push_back(kraken(E - 480 * 60 + i * 60)); }
    fr.push_back(kraken(E, "9"));
    auto p = parseMarketCandles(response(array(pr)), gc), f = parseMarketCandles(response(krakenBody(fr)), kc);
    CHECK(p.status == MarketDataStatus::Ok && p.value && f.value);
    CHECK(f.value->minutes.size() == 480 && f.value->committedCursor == E);
    auto display = marketWindow(*p.value, &*f.value, E - 3600, E);
    CHECK(display.expected == 60 && display.pearlCovered == 60 && display.fxCovered == 60);
    CHECK(mean(display.tokenUsdt) == "2.000000" && mean(display.tokenUsd) == "2.000000");
    const auto economics = marketWindow(*p.value, &*f.value, E - 480 * 60, E);
    CHECK(economics.expected == 480 && mean(economics.tokenUsdt) == "2.000000");
    CHECK(!marketWindow(*p.value, &*f.value, E - 86400, E).tokenUsdt);
    std::reverse(pr.begin(), pr.end());
    auto reversed = parseMarketCandles(response(array(pr)), gc);
    CHECK(reversed.value && reversed.value->minutes[0].startUtc == E - 480 * 60);
    CHECK(mean(marketWindow(*reversed.value, nullptr, E - 3600, E).tokenUsdt) == "2.000000");
    pr.clear(); fr.clear();
    for (unsigned i = 0; i < 60; ++i) {
        const auto price = i % 2 ? "3" : "1";
        pr.push_back(gate(E - 3600 + i * 60, price)); fr.push_back(kraken(E - 3600 + i * 60, price));
    }
    fr.push_back(kraken(E, "9"));
    p = parseMarketCandles(response(array(pr)), gc); f = parseMarketCandles(response(krakenBody(fr)), kc);
    display = marketWindow(*p.value, &*f.value, E - 3600, E);
    CHECK(mean(display.tokenUsdt) == "2.000000"); CHECK(mean(display.tokenUsd) == "5.000000");  // not 4 (product of means)
    auto missing = pr; missing.erase(missing.begin()); missing.push_back(gate(E - 3660));
    auto partial = parseMarketCandles(response(array(missing)), gc);
    CHECK(partial.value && !marketWindow(*partial.value, &*f.value, E - 3600, E).tokenUsdt);
    auto fxMissing = fr; fxMissing.erase(fxMissing.begin());
    auto partialFx = parseMarketCandles(response(krakenBody(fxMissing)), kc);
    display = marketWindow(*p.value, &*partialFx.value, E - 3600, E);
    CHECK(display.tokenUsdt && !display.tokenUsd && display.fxCovered == 59);
    auto unclosed = pr; unclosed[0] = gate(E - 3600, "1", "false"); unclosed.push_back(gate(E, "2", "false"));
    partial = parseMarketCandles(response(array(unclosed)), gc);
    CHECK(partial.value && !marketWindow(*partial.value, nullptr, E - 3600, E).tokenUsdt);
    auto duplicate = pr; duplicate.push_back(pr[0]);
    CHECK(parseMarketCandles(response(array(duplicate)), gc).status == MarketDataStatus::Timestamp);
    for (const auto &bad : {"[\"1799999940\",\"0\",\"2\",\"3\",\"1\",\"2\",\"true\"]",  // ambiguous seven positions
                            "[\"1799999940\",\"0\",\"2\",\"3\",\"1\",\"2\",\"0\",true]",
                            "[\"1799999940\",\"0\",\"2\",\"3\",\"1\",\"2\",\"0\",1]"}) CHECK(!parseMarketCandles(response("[" + std::string(bad) + "]"), gc).value);
    for (auto closure : {"TRUE", "1", "", "closed"}) CHECK(!parseMarketCandles(response(array({gate(E - 60, "2", closure)})), gc).value);
    for (uint64_t t : {E - 1, E + 60, E * 1000}) CHECK(!parseMarketCandles(response(array({gate(t)})), gc).value);
    CHECK(!parseMarketCandles(response(array({gate(E)})), gc).value);  // supposedly closed future interval
    auto unqualified = gc; unqualified.convention = MarketMinuteConvention::Unqualified;
    CHECK(parseMarketCandles(response(array(pr)), unqualified).status == MarketDataStatus::UnsupportedConvention);
    unqualified = gc; unqualified.origin = MarketOrigin::ProviderResponse;
    CHECK(parseMarketCandles(response(array(pr)), unqualified).status == MarketDataStatus::UnsupportedConvention);
    auto oldTerminal = fr; oldTerminal.back() = kraken(E - 3660, "9");
    f = parseMarketCandles(response(krakenBody(oldTerminal)), kc);
    CHECK(f.value && f.value->minutes.size() == 60);
    CHECK(mean(marketWindow(*p.value, &*f.value, E - 3600, E).tokenUsd) == "5.000000");
    CHECK(!parseMarketCandles(response(array(std::vector<std::string>(1001, gate(E - 60)))), gc).value);
    CHECK(!parseMarketCandles(response(krakenBody(std::vector<std::string>(721, kraken(E - 60)))), kc).value);
    for (const auto &price : {"0", "-1", "NaN", "inf", "1e2", "01", "0.0000000000000000001", "11", "0.01"})
        CHECK(!parseMarketCandles(response(array({gate(E - 60, price)})), gc).value);
    auto gt = context(PublicFeed::GatePearlTicker), kt = context(PublicFeed::KrakenUsdtTicker);
    const std::string pearlTicker = "[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.50\",\"highest_bid\":\"2.49\",\"lowest_ask\":\"2.51\",\"etf_pre_timestamp\":123}]";
    const std::string fxTicker = "{\"error\":[],\"result\":{\"USDTZUSD\":{\"a\":[\"0.99\",\"1\",\"1\"],\"b\":[\"0.97\",\"1\",\"1\"],\"c\":[\"0.98\",\"1\"]}}}";
    auto pq = parseMarketQuote(response(pearlTicker), gt), fq = parseMarketQuote(response(fxTicker), kt);
    CHECK(pq.value && fq.value && !pq.value->sourceEventUtc && !fq.value->sourceEventUtc);
    auto latest = latestMarketReference(&*pq.value, &*fq.value, E + 10, 100000);
    CHECK(latest.tokenUsd && latest.tokenUsd->text() == "2.45"); CHECK(latest.tokenUsdt->text() == "2.5");
    CHECK(!latestMarketReference(&*pq.value, nullptr, E + 10, 100000).tokenUsd);
    CHECK(latestMarketReference(&*pq.value, nullptr, E + 10, 100000).tokenUsdt.has_value());
    CHECK(!latestMarketReference(&*pq.value, &*fq.value, E + 10, 100000, {120000,30000,true}).tokenUsd);
    CHECK(latestMarketReference(&*pq.value, &*fq.value, E + 130, 220000).tokenUsd.has_value());
    CHECK(!latestMarketReference(&*pq.value, &*fq.value, E + 130, 220001).tokenUsd);
    CHECK(!latestMarketReference(&*pq.value, &*fq.value, E + 131, 220000).tokenUsd);
    CHECK(!latestMarketReference(&*pq.value, &*fq.value, E + 9, 100000).tokenUsd);
    CHECK(!latestMarketReference(&*pq.value, &*fq.value, E + 10, 99999).tokenUsd);
    auto olderFx = *fq.value; olderFx.observation.receivedMonoMs -= 30000; olderFx.observation.observedUtc -= 30; olderFx.observation.requestStartedUtc -= 30;
    CHECK(latestMarketReference(&*pq.value, &olderFx, E + 10, 100000).tokenUsd.has_value());
    --olderFx.observation.receivedMonoMs;
    CHECK(latestMarketReference(&*pq.value, &olderFx, E + 10, 100000).usdStatus == MarketFreshness::Skew);
    olderFx = *fq.value; ++olderFx.observation.generation;
    CHECK(!latestMarketReference(&*pq.value, &olderFx, E + 10, 100000).tokenUsd);
    CHECK(!latestMarketReference(&*fq.value, &*pq.value, E + 10, 100000).tokenUsd);
    for (const auto &ticker : {"[{\"currency_pair\":\"PRL_USDT\",\"last\":\"2\"}]", "[{\"currency_pair\":\"PEARL_USD\",\"last\":\"2\"}]",
                               "[{\"currency_pair\":\"PEARL_USDT\",\"last\":2}]", "[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"0\"}]",
                               "[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2\",\"highest_bid\":\"3\",\"lowest_ask\":\"1\"}]", "[]"}) CHECK(!parseMarketQuote(response(ticker), gt).value);
    for (const auto &ticker : {"{\"error\":[\"EAPI:Rate limit exceeded\"],\"result\":{}}", "{\"error\":[],\"result\":{\"USDTUSD\":{\"c\":[\"1\",\"1\"]}}}",
                               "{\"error\":[],\"result\":{\"USDTZUSD\":{\"c\":[\"1\",\"1\"]},\"USDZUSDT\":{}}}"}) CHECK(!parseMarketQuote(response(ticker), kt).value);
    CHECK(parseMarketQuote(response("{\"error\":[\"EAPI:Rate limit exceeded\"]}"), kt).status == MarketDataStatus::RateLimited);
    CHECK(parseMarketQuote(response("{\"error\":[\"unrecognized synthetic provider error\"]}"), kt).status == MarketDataStatus::ProviderError);
    auto badHttp = response(pearlTicker); badHttp.httpStatus = 503;
    CHECK(parseMarketQuote(badHttp, gt).status == MarketDataStatus::Transport);
    auto wrongTime = response(pearlTicker); wrongTime.observedUtc = E;
    CHECK(parseMarketQuote(wrongTime, gt).status == MarketDataStatus::Timestamp);
    wrongTime.observedUtc = E + 11; CHECK(parseMarketQuote(wrongTime, gt).status == MarketDataStatus::Timestamp);
    auto changed = *p.value; changed.minutes.push_back(changed.minutes.back());
    CHECK(!marketWindow(changed, nullptr, E - 3600, E).tokenUsdt);
    changed = *p.value; std::reverse(changed.minutes.begin(), changed.minutes.end());
    CHECK(!marketWindow(changed, nullptr, E - 3600, E).tokenUsdt);
    auto wrongOrigin = *fq.value; wrongOrigin.observation.origin = MarketOrigin::ProviderResponse;
    CHECK(!latestMarketReference(&*pq.value, &wrongOrigin, E + 10, 100000).tokenUsd);
    CHECK(!parseMarketQuote(response(pearlTicker), gc).value);
    return finishTests("market_data");
}
