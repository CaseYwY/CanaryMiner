// Development-only native preview. Every price/observation is synthetic; no file/network, GPU or miner access.
#include "product/dashboard.h"
#include "product/market_owner.h"
#include "product/json_dom.h"
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const std::string mode = argv[1];
    if (mode != "fresh" && mode != "stale" && mode != "missing-fx" && mode != "clock-fault") return 2;
    unsigned width = 0;
    for (const char *p = argv[2]; *p; ++p) { if (*p < '0' || *p > '9' || width > 180) return 2; width = width * 10 + static_cast<unsigned>(*p - '0'); }
    if (width < 40 || width > 180) return 2;
    constexpr uint64_t E = 1800000000;
    om::MarketSnapshot snapshot;
    snapshot.running = true; snapshot.generation = 1; snapshot.origin = om::MarketOrigin::SyntheticFixture;
    auto parseContext = [&](om::PublicFeed feed) {
        const bool candle = feed == om::PublicFeed::GatePearlCandles || feed == om::PublicFeed::KrakenUsdtCandles;
        return om::MarketParseContext{{feed, candle ? static_cast<int64_t>(E - 3600) : 0, candle ? static_cast<int64_t>(E) : 0}, E + 9, 100000, 1,
            om::MarketOrigin::SyntheticFixture, om::MarketMinuteConvention::FixtureMinuteStart, E + 10};
    };
    auto response = [E](std::string body) {
        om::PublicJsonResponse r; r.status = om::PublicHttpsStatus::Ok; r.httpStatus = 200; r.observedUtc = E + 10; r.body = std::move(body); return r;
    };
    snapshot.pearlQuote = om::parseMarketQuote(response("[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.5\"}]"), parseContext(om::PublicFeed::GatePearlTicker)).value;
    snapshot.fxQuote = om::parseMarketQuote(response("{\"error\":[],\"result\":{\"USDTZUSD\":{\"c\":[\"0.98\",\"1\"]}}}"), parseContext(om::PublicFeed::KrakenUsdtTicker)).value;
    std::string pearl = "[", fx = "{\"error\":[],\"result\":{\"USDTZUSD\":[";
    for (unsigned i = 0; i < 60; ++i) {
        const auto t = std::to_string(E - 3600 + i * 60), p = i % 2 ? std::string("3") : std::string("1");
        if (i) { pearl += ','; fx += ','; }
        pearl += "[" + om::jsonQuote(t) + ",\"0\"," + om::jsonQuote(p) + ",\"3\",\"1\"," + om::jsonQuote(p) + ",\"0\",\"true\"]";
        fx += "[" + t + ',' + om::jsonQuote(p) + ",\"3\",\"1\"," + om::jsonQuote(p) + ',' + om::jsonQuote(p) + ",\"0\",0]";
    }
    pearl += ']'; fx += ",[1800000000,\"1\",\"3\",\"1\",\"1\",\"1\",\"0\",0]],\"last\":1800000000}}";
    snapshot.pearlCandles = om::parseMarketCandles(response(pearl), parseContext(om::PublicFeed::GatePearlCandles)).value;
    snapshot.fxCandles = om::parseMarketCandles(response(fx), parseContext(om::PublicFeed::KrakenUsdtCandles)).value;
    for (auto &h : snapshot.health) { h.transport = om::PublicHttpsStatus::Ok; h.data = om::MarketDataStatus::Ok; }
    uint64_t utc = E + 10, mono = 100000;
    if (mode == "missing-fx") { snapshot.fxQuote.reset(); snapshot.fxCandles.reset(); snapshot.health[2].transport = om::PublicHttpsStatus::Unavailable; }
    if (mode == "stale") { utc += 121; mono += 121000; }
    if (mode == "clock-fault") snapshot.clockFault = true;
    om::DashModel model; model.version = "SYNTHETIC MARKET PREVIEW"; model.connection = "Stopped";
    model.publicMarket = om::marketDashboardView(snapshot, {}, utc, mono);
    om::DashView view; view.columns = static_cast<int>(width); view.rows = 0; view.color = om::ColorLevel::None;
    std::cout << om::renderDashboard(model, view);
    return 0;
}
