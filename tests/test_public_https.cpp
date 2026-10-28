// Pure validation and backend availability only; no network, credentials or miner.
#include "product/public_https.h"
#include <array>
#include "product_test_util.h"

using namespace om;

int main() {
    std::string url = "unchanged";
    CHECK(publicFeedUrl({}, &url));
    CHECK(url == "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=PEARL_USDT");
    CHECK(publicFeedUrl({PublicFeed::KrakenUsdtTicker}, &url));
    CHECK(url == "https://api.kraken.com/0/public/Ticker?pair=USDTUSD");
    CHECK(publicFeedUrl({PublicFeed::GatePearlCandles, 600, 600 + 900 * 60}, &url));
    CHECK(url.find("from=540&to=54600") != std::string::npos);
    CHECK(publicFeedUrl({PublicFeed::KrakenUsdtCandles, 600, 600 + 719 * 60}, &url));
    for (auto r : {PublicJsonRequest{static_cast<PublicFeed>(99)},
                   PublicJsonRequest{PublicFeed::GatePearlTicker, 60, 120},
                   PublicJsonRequest{PublicFeed::GatePearlCandles, 0, 60},
                   PublicJsonRequest{PublicFeed::GatePearlCandles, -60, 60},
                   PublicJsonRequest{PublicFeed::GatePearlCandles, 60, 60},
                   PublicJsonRequest{PublicFeed::GatePearlCandles, 61, 120},
                   PublicJsonRequest{PublicFeed::GatePearlCandles, 60, 61},
                   PublicJsonRequest{PublicFeed::GatePearlCandles, 60, 60 + 901 * 60},
                   PublicJsonRequest{PublicFeed::KrakenUsdtCandles, 60, 60 + 720 * 60},
                   PublicJsonRequest{PublicFeed::KrakenUsdtCandles, INT64_MAX - 60, INT64_MAX}}) {
        url = "unchanged";
        CHECK(!publicFeedUrl(r, &url));
        CHECK(url == "unchanged");
    }
    CHECK(!publicFeedUrl({}, nullptr));
    CHECK(!publicIpv4(nullptr));
    for (auto text : {"", " ", "{", "{}[]", "[1,]", "{\"a\":1,\"a\":2}", "{\"a\":1,\"\\u0061\":2}",
                      "{\"a\":NaN}", "\"\\ud800\"", "\"\\udc00\"", "\"bad\ntext\"", "[01]", "[+1]", "[1.]"}) CHECK(!boundedPublicJson(text));
    for (auto text : {"{}", "[]", "{\"a\": [null,true,false,-1.5e2,\"x\"]}", "\"\\ud83d\\ude00\"", "\"\xe4\xb8\xad\""}) CHECK(boundedPublicJson(text));
    for (std::string text : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80", "\xe4\xb8"}) CHECK(!boundedPublicJson("\"" + text + "\""));
    CHECK(!boundedPublicJson(std::string(17, '[') + "0" + std::string(17, ']')));
    CHECK(boundedPublicJson(std::string(16, '[') + "0" + std::string(16, ']')));
    CHECK(!boundedPublicJson("\"" + std::string(4097, 'a') + "\""));
    CHECK(!boundedPublicJson(std::string(1024 * 1024 + 1, ' ')));
    CHECK(!boundedPublicJson("[" + std::string(129, '1') + "]"));
    std::string object = "{";
    for (int i = 0; i < 257; ++i) object += (i ? "," : "") + std::string("\"key") + std::to_string(i) + "\":0";
    CHECK(!boundedPublicJson(object + "}"));
    std::string array = "[0";
    for (int i = 0; i < 20000; ++i) array += ",0";
    CHECK(!boundedPublicJson(array + "]"));
    for (auto b : {std::array<uint8_t,4>{0,1,2,3}, {10,2,3,4}, {100,64,0,1}, {100,127,255,255}, {127,0,0,1},
                   {169,254,0,1}, {172,16,0,1}, {172,31,255,255}, {192,0,0,1}, {192,0,2,1}, {192,168,1,1},
                   {192,88,99,1}, {198,18,0,1}, {198,19,0,1}, {198,51,100,1}, {203,0,113,1}, {224,0,0,1}, {255,255,255,255}}) CHECK(!publicIpv4(b.data()));
    for (auto b : {std::array<uint8_t,4>{1,1,1,1}, {8,8,8,8}, {100,63,255,255}, {100,128,0,0}, {172,15,255,255}, {172,32,0,0}, {223,255,255,254}}) CHECK(publicIpv4(b.data()));
    uint8_t v6[16] = {};
    CHECK(!publicIpv6(v6));
    v6[15] = 1; CHECK(!publicIpv6(v6));
    v6[0] = 0xfe; v6[1] = 0x80; CHECK(!publicIpv6(v6));
    v6[0] = 0x20; v6[1] = 1; v6[2] = 0x0d; v6[3] = 0xb8; CHECK(!publicIpv6(v6));
    v6[1] = 2; CHECK(!publicIpv6(v6));
    v6[1] = 1; v6[2] = 0; v6[3] = 0x20; CHECK(!publicIpv6(v6));
    v6[0] = 0x3f; v6[1] = 0xff; v6[2] = 1; CHECK(!publicIpv6(v6));
    v6[0] = 0x26; v6[1] = 6; v6[2] = 0x47; v6[3] = 0; CHECK(publicIpv6(v6));
    CHECK(!publicIpv6(nullptr));
    PublicJsonClient client;
    std::atomic<bool> cancel{true};
    CHECK(client.get({}, cancel).status == PublicHttpsStatus::Cancelled);
    cancel = false;
    CHECK(client.get({}, cancel, {0, 100}).status == PublicHttpsStatus::InvalidRequest);
    CHECK(client.get({}, cancel, {100, 50}).status == PublicHttpsStatus::InvalidRequest);
    CHECK(client.get({}, cancel, {100, 60001}).status == PublicHttpsStatus::InvalidRequest);
    CHECK(client.get({}, cancel, {100, 200, 0}).status == PublicHttpsStatus::InvalidRequest);
    CHECK(client.get({}, cancel, {100, 200, 1024 * 1024 + 1}).status == PublicHttpsStatus::InvalidRequest);
    CHECK(client.get({}, cancel, {100, 200, 10, 63}).status == PublicHttpsStatus::InvalidRequest);
    CHECK(client.get({}, cancel, {100, 200, 10, 32769}).status == PublicHttpsStatus::InvalidRequest);
#if !defined(CANARY_EXPECT_HTTPS)
    CHECK(!client.available());
    CHECK(client.get({}, cancel).status == PublicHttpsStatus::Unavailable);
#else
    CHECK(client.available());
#endif
    return finishTests("public_https");
}
