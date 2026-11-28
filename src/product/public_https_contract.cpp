// Shared validation for the miner's IPC and the public JSON helper.
#include "public_https.h"
#include "json_dom.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <utility>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace om {

const char *publicHttpsStatusName(PublicHttpsStatus status) {
    switch (status) {
        case PublicHttpsStatus::Ok: return "ok";
        case PublicHttpsStatus::Unavailable: return "verified HTTPS/c-ares backend unavailable";
        case PublicHttpsStatus::InvalidRequest: return "invalid public request or bounds";
        case PublicHttpsStatus::Busy: return "public client already has an owner";
        case PublicHttpsStatus::Cancelled: return "cancelled";
        case PublicHttpsStatus::Timeout: return "public request deadline exceeded";
        case PublicHttpsStatus::DestinationBlocked: return "non-public destination refused";
        case PublicHttpsStatus::TlsVerification: return "TLS certificate/name/trust verification failed";
        case PublicHttpsStatus::TlsProtocol: return "TLS handshake/protocol failed";
        case PublicHttpsStatus::Network: return "public request network failure";
        case PublicHttpsStatus::HttpStatus: return "public provider HTTP error";
        case PublicHttpsStatus::InvalidHeaders: return "invalid/unsupported public response headers";
        case PublicHttpsStatus::BodyTooLarge: return "public response body limit exceeded";
        case PublicHttpsStatus::InvalidJson: return "invalid/over-budget public JSON";
        case PublicHttpsStatus::AssetIdentity: return "public market asset identity could not be verified";
        case PublicHttpsStatus::Unreaped: return "public helper exit/ownership unconfirmed; admission blocked";
    }
    return "unsupported public response";
}

bool validOctopusRateProduct(std::string_view product) {
    for (const std::string_view kind : {"AGILE-FLEX-", "AGILE-", "GO-VAR-"}) {
        if (product.substr(0, kind.size()) != kind) continue;
        auto date = product.substr(kind.size());
        if (date.size() != 8 || date[2] != '-' || date[5] != '-' ||
            date.substr(0, 2).find_first_not_of("0123456789") != date.npos ||
            date.substr(3, 2).find_first_not_of("0123456789") != date.npos ||
            date.substr(6, 2).find_first_not_of("0123456789") != date.npos) continue;
        const auto pair = [&](size_t at) { return (date[at] - '0') * 10 + date[at + 1] - '0'; };
        const auto year = pair(0), month = pair(3), day = pair(6);
        const int days[] = {31, year % 4 == 0 ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return month >= 1 && month <= 12 && day >= 1 && day <= days[month - 1];
    }
    return false;
}

static std::string octopusPriceUtc(int64_t seconds) {
    const auto value = static_cast<std::time_t>(seconds);
    if (static_cast<int64_t>(value) != seconds) return {};
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &value)) return {};
#else
    if (!gmtime_r(&value, &utc)) return {};
#endif
    char stamp[32];
    const auto n = std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02dT%02d%%3A%02d%%3A%02dZ",
                                 utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
    return n == 24 ? std::string(stamp, static_cast<size_t>(n)) : std::string{};
}

bool publicFeedUrl(const PublicJsonRequest &r, std::string *url) {
    const bool unitRate = r.feed == PublicFeed::OctopusUnitRates;
    const bool octopus = unitRate || r.feed == PublicFeed::OctopusStandingCharges;
    if (!url || (r.blockHeight && r.feed != PublicFeed::PearlIndexedBlockHash) || octopus != r.octopus.has_value())
        return false;
    std::string result;
    switch (r.feed) {
        case PublicFeed::GatePearlAsset:
            if (r.fromUtc || r.toUtc) return false;
            result = "https://api.gateio.ws/api/v4/spot/currencies/PEARL"; break;
        case PublicFeed::MexcPearlPair:
            if (r.fromUtc || r.toUtc) return false;
            result = "https://api.mexc.com/api/v3/exchangeInfo?symbol=PEARLUSDT"; break;
        case PublicFeed::GatePearlTicker:
            if (r.fromUtc || r.toUtc) return false;
            result = "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=PEARL_USDT"; break;
        case PublicFeed::KrakenUsdtTicker:
            if (r.fromUtc || r.toUtc) return false;
            result = "https://api.kraken.com/0/public/Ticker?pair=USDTUSD"; break;
        case PublicFeed::GatePearlTrades:
            if (r.fromUtc || r.toUtc) return false;
            result = "https://api.gateio.ws/api/v4/spot/trades?currency_pair=PEARL_USDT&limit=" +
                std::to_string(maxPublicTradeRows);
            break;
        case PublicFeed::KrakenUsdtTrades:
            if (r.fromUtc || r.toUtc) return false;
            result = "https://api.kraken.com/0/public/Trades?pair=USDTUSD&count=" +
                std::to_string(maxPublicTradeRows);
            break;
        case PublicFeed::PearlMainnetTip:
            if (r.fromUtc || r.toUtc) return false;
            result = "https://blockbook.pearlresearch.ai/api/status"; break;
        case PublicFeed::PearlIndexedBlockHash:
            if (r.fromUtc || r.toUtc || !r.blockHeight || *r.blockHeight > maxPearlRewardTipHeight) return false;
            result = "https://blockbook.pearlresearch.ai/api/v2/block-index/" + std::to_string(*r.blockHeight);
            break;
        case PublicFeed::GatePearlCandles:
        case PublicFeed::KrakenUsdtCandles: {
            const int64_t maximum = r.feed == PublicFeed::GatePearlCandles ? 900 * 60 : 719 * 60;
            if (r.fromUtc < 60 || r.toUtc <= r.fromUtc || r.toUtc > 253402300799LL ||
                r.fromUtc % 60 || r.toUtc % 60 || r.toUtc - r.fromUtc > maximum) return false;
            if (r.feed == PublicFeed::GatePearlCandles)
                result = "https://api.gateio.ws/api/v4/spot/candlesticks?currency_pair=PEARL_USDT&interval=1m&from=" +
                    std::to_string(r.fromUtc - 60) + "&to=" + std::to_string(r.toUtc);
            else result = "https://api.kraken.com/0/public/OHLC?pair=USDTUSD&interval=1&since=" + std::to_string(r.fromUtc);
            break;
        }
        case PublicFeed::MexcPearlHour:
        case PublicFeed::CoinbaseUsdtHour:
            if (r.fromUtc < 60 || r.toUtc > 253402300799LL || r.toUtc <= r.fromUtc ||
                r.toUtc - r.fromUtc != 3600 || r.fromUtc % 60) return false;
            if (r.feed == PublicFeed::MexcPearlHour)
                result = "https://api.mexc.com/api/v3/klines?symbol=PEARLUSDT&interval=1m&limit=61&startTime=" +
                    std::to_string(r.fromUtc * 1000) + "&endTime=" + std::to_string(r.toUtc * 1000 - 1);
            else
                result = "https://api.coinbase.com/api/v3/brokerage/market/products/USDT-USD/candles?granularity=ONE_MINUTE&limit=61&start=" +
                    std::to_string(r.fromUtc) + "&end=" + std::to_string(r.toUtc);
            break;
        case PublicFeed::OctopusUnitRates:
        case PublicFeed::OctopusStandingCharges: {
            const auto &selector = *r.octopus;
            const auto rowLimit = unitRate ? octopusMaxUnitRows : octopusMaxStandingRows;
            if (!validOctopusRateProduct(selector.product) || octopusRateRegions.find(selector.region) == std::string_view::npos ||
                !selector.page || selector.page > octopusMaxPages(rowLimit) ||
                r.fromUtc < 946684800 || r.toUtc > 4102444800LL || r.toUtc <= r.fromUtc ||
                r.fromUtc % 1800 || r.toUtc % 1800 || r.toUtc - r.fromUtc > octopusMaxUnitRows * 1800LL) return false;
            const auto begin = octopusPriceUtc(r.fromUtc);
            const auto end = octopusPriceUtc(r.toUtc - 1); // Provider's price query end is inclusive.
            if (begin.empty() || end.empty()) return false;
            result = "https://api.oegb-kraken.energy/v1/products/" + selector.product +
                "/electricity-tariffs/E-1R-" + selector.product + '-' + selector.region +
                (unitRate ? "/standard-unit-rates/" : "/standing-charges/") + "?period_from=" + begin +
                "&period_to=" + end + "&page_size=" + std::to_string(octopusRatePageSize) + "&page=" + std::to_string(selector.page);
            break;
        }
        default: return false;
    }
    *url = std::move(result);
    return true;
}

bool publicIpv4(const uint8_t *b) {
    if (!b || b[0] == 0 || b[0] == 10 || b[0] == 127 || b[0] >= 224 ||
        (b[0] == 100 && b[1] >= 64 && b[1] <= 127) || (b[0] == 169 && b[1] == 254) ||
        (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168) ||
        (b[0] == 192 && b[1] == 0 && (b[2] == 0 || b[2] == 2)) ||
        (b[0] == 192 && b[1] == 88 && b[2] == 99) || (b[0] == 198 && (b[1] == 18 || b[1] == 19)) ||
        (b[0] == 198 && b[1] == 51 && b[2] == 100) || (b[0] == 203 && b[1] == 0 && b[2] == 113)) return false;
    return true;
}
bool publicIpv6(const uint8_t *b) {
    if (!b || (b[0] & 0xe0) != 0x20) return false;  // global unicast only; no mapped/compatible IPv4, local or multicast
    if (b[0] == 0x20 && b[1] == 1 && (b[2] & 0xfe) == 0) return false;  // conservative 2001::/23 special-use exclusion
    if (b[0] == 0x20 && b[1] == 1 && b[2] == 0x0d && b[3] == 0xb8) return false;
    if (b[0] == 0x20 && b[1] == 2) return false;  // 6to4 embeds another endpoint
    if (b[0] == 0x3f && b[1] == 0xff && (b[2] & 0xf0) == 0) return false;  // documentation 3fff::/20
    return true;
}

bool validPublicDnsServer(std::string_view address) {
    if (address == publicCloudflareDoh) return true;
    if (address.empty() || address.size() >= INET6_ADDRSTRLEN ||
        address.find_first_not_of("0123456789abcdefABCDEF:.") != std::string_view::npos) return false;
    char literal[INET6_ADDRSTRLEN]{};
    std::copy(address.begin(), address.end(), literal);
    uint8_t bytes[16]{};
    if (inet_pton(AF_INET, literal, bytes) == 1) {
        // The DNS socket may address a local or shared resolver. Payload sockets use publicIpv4 alone.
        return publicIpv4(bytes) || bytes[0] == 10 || bytes[0] == 127 ||
            (bytes[0] == 100 && bytes[1] >= 64 && bytes[1] <= 127) ||
            (bytes[0] == 169 && bytes[1] == 254) ||
            (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) || (bytes[0] == 192 && bytes[1] == 168);
    }
    if (address.find('.') != std::string_view::npos) {
        const auto colon = address.rfind(':');
        // Apply the same strict dotted-decimal spelling on both socket APIs.
        if (colon == std::string_view::npos || inet_pton(AF_INET, literal + colon + 1, bytes) != 1) return false;
    }
    if (inet_pton(AF_INET6, literal, bytes) != 1) return false;
    return publicIpv6(bytes) || (bytes[0] & 0xfe) == 0xfc ||
        (bytes[15] == 1 && std::all_of(bytes, bytes + 15, [](uint8_t value) { return value == 0; }));
}

static bool validUtf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i++]);
        if (b < 0x80) continue;
        unsigned count = b >= 0xc2 && b <= 0xdf ? 1 : b >= 0xe0 && b <= 0xef ? 2 : b >= 0xf0 && b <= 0xf4 ? 3 : 0;
        if (!count || s.size() - i < count) return false;
        const auto second = static_cast<unsigned char>(s[i]);
        if ((b == 0xe0 && second < 0xa0) || (b == 0xed && second >= 0xa0) ||
            (b == 0xf0 && second < 0x90) || (b == 0xf4 && second >= 0x90)) return false;
        while (count--) if ((static_cast<unsigned char>(s[i++]) & 0xc0) != 0x80) return false;
    }
    return true;
}
bool boundedPublicJson(const std::string &text, std::size_t byteLimit, std::size_t nodeLimit) {
    if (text.empty() || text.size() > byteLimit || !validUtf8(text)) return false;
    size_t nodes = 0, depth = 0, stringBytes = 0, literalBytes = 0;
    size_t members[17] = {};
    bool quoted = false, escaped = false;
    for (unsigned char ch : text) {
        if (quoted) {
            if (++stringBytes > 4096) return false;
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
            continue;
        }
        if (ch == '"') { quoted = true; stringBytes = 0; ++nodes; literalBytes = 0; }
        else if (ch == '{' || ch == '[') { if (++depth > 16) return false; members[depth] = 0; ++nodes; literalBytes = 0; }
        else if (ch == '}' || ch == ']') { if (!depth) return false; --depth; literalBytes = 0; }
        else if (ch == ':') { if (++members[depth] > 256) return false; literalBytes = 0; }
        else if (ch == ',' || ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') literalBytes = 0;
        else { if (!literalBytes) ++nodes; if (++literalBytes > 128) return false; }
        if (nodes > nodeLimit) return false;
    }
    if (quoted || depth) return false;
    JsonValue value;
    return parseJsonStrict(text, &value, nullptr, {byteLimit, 256});  // never echo provider keys/payload as a diagnostic
}

}  // namespace om
