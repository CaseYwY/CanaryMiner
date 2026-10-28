// Bounded, unauthenticated public JSON transport. No wallets, arbitrary URLs, pool I/O or trades.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace om {

enum class PublicFeed { GatePearlTicker, GatePearlCandles, KrakenUsdtTicker, KrakenUsdtCandles };
struct PublicJsonRequest {
    PublicFeed feed = PublicFeed::GatePearlTicker;
    int64_t fromUtc = 0, toUtc = 0;  // minute bounds for candles; zero for ticker requests
};
struct PublicHttpsLimits {
    unsigned connectMs = 5000, totalMs = 15000;
    size_t bodyBytes = 1024 * 1024, headerBytes = 32 * 1024;
};
enum class PublicHttpsStatus {
    Ok, Unavailable, InvalidRequest, Busy, Cancelled, Timeout, DestinationBlocked,
    TlsVerification, TlsProtocol, Network, HttpStatus, InvalidHeaders, BodyTooLarge, InvalidJson
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
bool boundedPublicJson(const std::string &text);

class PublicJsonClient {
   public:
    // Low-level helper-side implementation. Native trust APIs may block: the application must use
    // PublicJsonProcessClient from its own background data worker, not call this in the miner process.
    PublicJsonClient();
    ~PublicJsonClient();
    PublicJsonClient(const PublicJsonClient &) = delete;
    PublicJsonClient &operator=(const PublicJsonClient &) = delete;
    bool available() const;
    PublicJsonResponse get(const PublicJsonRequest &request, const std::atomic<bool> &cancel,
                           PublicHttpsLimits limits = {});
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
#ifdef CANARY_HTTPS_TESTING
    friend struct PublicHttpsTestPeer;
#endif
};

#ifdef CANARY_HTTPS_TESTING
// Compiled only in the separate loopback-integration library, never the production target.
struct PublicHttpsTestPeer {
    static std::unique_ptr<PublicJsonClient> create(unsigned port, std::string path, std::string caFile,
                                                   bool wrongName = false, bool permitLoopback = true);
};
#endif

}  // namespace om
