#pragma once

#include "market_sample_options.h"
#include <csignal>

namespace om {

struct PublicTickerReceipt {
    uint64_t sentUtc = 0, receivedUtc = 0, elapsedMs = 0;
    PublicJsonResponse response;
};
struct PublicTickerCapture {
    uint64_t startedUtc = 0, cutoffUtc = 0, elapsedMs = 0;
    PublicTickerReceipt gate, kraken;
    bool trades = false;  // Parent UTC fields, including receipt sent/received, use milliseconds for trades.
};
enum class PublicQuoteReport { InvalidClock, InvalidEvidence, Incomplete, Complete };
PublicQuoteReport renderPublicTradeSample(const PublicTickerCapture &capture, std::string &report,
                                          bool withReceipts = false);
void appendPublicQuoteReceipt(std::string &report, const PublicTickerReceipt &receipt, bool trades);

// Reads one explicit local file, independently of the installed HTTPS helper.
int commandTickerFile(MarketTickerAction action, int argc, const char *const *argv,
                      const volatile sig_atomic_t *stop);
class NativeProcessLifetime;
int savePublicTickerReview(const std::string &name, const PublicTickerCapture &capture,
                           const NativeProcessLifetime &lifetime, const volatile sig_atomic_t *stop,
                           bool saveIncomplete);

}  // namespace om
