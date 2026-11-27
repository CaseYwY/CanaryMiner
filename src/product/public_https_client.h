// HTTPS transport for the public JSON worker and isolated backend tests.
#pragma once

#include "public_https.h"
#include <atomic>
#include <memory>

namespace om {

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
    static bool hasDohBackend();
    static std::unique_ptr<PublicJsonClient> create(unsigned port, std::string path, std::string caFile,
                                                   bool wrongName = false, bool permitLoopback = true,
                                                   std::string dohPath = {});
};
#endif

}  // namespace om
