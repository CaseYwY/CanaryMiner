// Bounded, unauthenticated public JSON transport. No wallets, arbitrary URLs, pool I/O or trading.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace om {

enum class PublicFeed {
    GatePearlTicker, GatePearlCandles, KrakenUsdtTicker, KrakenUsdtCandles,
    MexcPearlHour, CoinbaseUsdtHour, PearlMainnetTip, GatePearlTrades, KrakenUsdtTrades, PearlIndexedBlockHash,
    OctopusUnitRates, OctopusStandingCharges
};
inline constexpr uint32_t maxPearlRewardTipHeight = INT32_MAX - 1;
inline constexpr size_t maxPublicTradeRows=20;
inline constexpr size_t maxPublicTradeBodyBytes=64*1024;
inline constexpr std::string_view octopusRateRegions = "ABCDEFGHJKLMNP";
inline constexpr unsigned octopusRatePageSize = 100, octopusMaxUnitRows = 8192, octopusMaxStandingRows = 256;
inline constexpr unsigned octopusMaxPages(unsigned rows) { return (rows + octopusRatePageSize - 1) / octopusRatePageSize; }
bool validOctopusRateProduct(std::string_view product);

struct OctopusRatePage {
    std::string product;
    char region = 0;
    unsigned page = 0;

    bool operator==(const OctopusRatePage &other) const {
        return product == other.product && region == other.region && page == other.page;
    }
};

struct PublicJsonRequest {
    PublicFeed feed = PublicFeed::GatePearlTicker;
    int64_t fromUtc = 0, toUtc = 0;  // UTC seconds for candle and Octopus price windows
    std::optional<uint32_t> blockHeight{};  // Pearl indexed block hash only
    std::optional<OctopusRatePage> octopus{};
};
struct PublicHttpsLimits {
    unsigned connectMs = 5000, totalMs = 15000;
    size_t bodyBytes = 1024 * 1024, headerBytes = 32 * 1024;
    std::string dnsServer{};  // optional resolver for this request, never a payload destination
    // Parent-only deadline shared across requests. Each helper receives a bounded relative timeout.
    std::chrono::steady_clock::time_point parentDeadline = std::chrono::steady_clock::time_point::max();
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
