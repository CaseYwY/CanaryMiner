// Process boundary for TLS trust/resolver calls that the OS may not make cancellable.
#pragma once
#include "public_https.h"
#include <filesystem>

namespace om {

// The application supplies its installed sibling helper's absolute path, never a provider-supplied executable.
// Call from the public-data worker, not the render/mining thread. One request/child at a time; no retries.
class PublicJsonProcessClient {
   public:
    explicit PublicJsonProcessClient(std::filesystem::path helper);
    ~PublicJsonProcessClient();
    PublicJsonProcessClient(const PublicJsonProcessClient &) = delete;
    PublicJsonProcessClient &operator=(const PublicJsonProcessClient &) = delete;
    PublicJsonResponse get(const PublicJsonRequest &request, const std::atomic<bool> &cancel, PublicHttpsLimits limits = {});
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Bounded versioned IPC, separate from provider JSON. No arbitrary text is emitted in error responses.
bool encodePublicJsonResponse(const PublicJsonResponse &response, std::string *wire);
bool decodePublicJsonResponse(const std::string &wire, size_t bodyLimit, PublicJsonResponse *response);

}  // namespace om
