// Bounded, unauthenticated public JSON transport. No wallets, arbitrary URLs, pool I/O or trades.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace om {

enum class PublicFeed {
    GatePearlTicker, GatePearlCandles, KrakenUsdtTicker, KrakenUsdtCandles,
    MexcPearlHour, CoinbaseUsdtHour, PearlMainnetTip
};
struct PublicJsonRequest {
    PublicFeed feed = PublicFeed::GatePearlTicker;
    int64_t fromUtc = 0, toUtc = 0;  // minute bounds for candles; zero otherwise
};
struct PublicHttpsLimits {
    unsigned connectMs = 5000, totalMs = 15000;
    size_t bodyBytes = 1024 * 1024, headerBytes = 32 * 1024;
    std::string dnsServer{};  // optional one-shot resolver address, never a payload destination
};
enum class PublicHttpsStatus {
    Ok, Unavailable, InvalidRequest, Busy, Cancelled, Timeout, DestinationBlocked,
    TlsVerification, TlsProtocol, Network, HttpStatus, InvalidHeaders, BodyTooLarge, InvalidJson,
    Unreaped  // parent-only ownership uncertainty; never accepted from CMJSON1 helper input
};
const char *publicHttpsStatusName(PublicHttpsStatus status);
struct PublicJsonResponse {
    PublicHttpsStatus status = PublicHttpsStatus::Unavailable;
    long httpStatus = 0;
    std::string body;  // published only after complete HTTPS/HTTP/JSON validation, not a financial fact
    uint64_t observedUtc = 0;
    unsigned retryAfterSeconds = 0;
};

// No DNS, sockets or side effects. Conservative public-address and bounded JSON contracts.
bool publicFeedUrl(const PublicJsonRequest &request, std::string *url);
bool publicIpv4(const uint8_t *bytes);
bool publicIpv6(const uint8_t *bytes);
// One unscoped unicast address. The backend uses port 53.
bool validPublicDnsServer(std::string_view address);
bool boundedPublicJson(const std::string &text);

}  // namespace om
